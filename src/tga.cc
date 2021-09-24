/* tga.cc implements a class to read and write TGA files. We support headers,
 * footers, color palettes and (simple) modifications of image attributes.
 *
 * renderer Copyright (C) 2021 Daniel Schuette
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include <algorithm>
#include <cstring>

#include "tga.hh"
#include "io.hh"

/* This constructor is used to read a TGA file at FILEPATH into memory. It can
 * then be modified and written back to disk. Note that we keep all data in
 * memory at all times. Right now, it seems like a premature optimization to
 * change that.
 */
TGA::TGA(std::string_view filepath) : TGA {}
{
    FILE* file_ptr = fopen(filepath.data(), "rb");
    if (file_ptr == nullptr) fail("cannot open file `", filepath, '\'');

    /* TGA is a simple file format with header, footer and variable-sized
     * fields inbetween them. TGA v2.0 defines a developer area and an
     * extension field, but we don't care about those. With warnings enabled,
     * we do print messages if they exist, though.
     */
    TGA::parse_header(file_ptr);

    /* TGA has 3 variable length fields (length in parens), that follow right
     * after the fixed-sized header:
     *  - Image ID (ID_LENGTH) -> optional, containing identifying info
     *  - Color map (COLOR_MAP_SPEC.LENGTH) -> table containing color map
     *  - Image data (IMAGE_SPEC) -> stored according to image descriptor
     */
    {
        size_t length = header.id_length;
        this->image_id_data.reserve(length);
        TGA::read_n_bytes(this->image_id_data.data(), length, "image id",
                          file_ptr);
    }

    {
        // Color map entries are stored using an integral number of bytes.
        size_t bytes_per_entry =
            (this->header.color_map_spec.bits_per_pixel / 8) +
            (this->header.color_map_spec.bits_per_pixel % 8 == 0 ? 0 : 1);
        size_t length = this->header.color_map_spec.length * bytes_per_entry;

        this->color_map.reserve(length);
        TGA::read_n_bytes(this->color_map.data(), length, "color map",
                          file_ptr);
    }

    if (this->header.color_map_type == 0x1) {
        assert((this->header.image_type & 0x7) == 0x1);
        assert(this->color_map.size() > 0);
        fail("color-mapped images aren't supported");
    }

    if (this->header.color_map_type == 0x0) {
        if ((this->header.image_type & 0x7) == 0x3)
            fail("gray-scale images aren't supported");
    }

    size_t length = this->header.image_spec.height *
        this->header.image_spec.width * this->get_pixel_width();
    bool is_rle = this->header.image_type & 0x8;
    if (!is_rle) {
        this->image_data.reserve(length);
        TGA::read_n_bytes(this->image_data.data(), length, "image data",
                          file_ptr);
    } else {
        // For RLE encoded images, we decode them right here.
        uint8_t* buf = new uint8_t[length];
        size_t count = fread(buf, sizeof(uint8_t), length, file_ptr);
        this->read_rle_image_data(buf, count, length);
        delete[] buf;
    }

    // Now, the image is no longer RLE encoded (even if it was before).
    this->header.image_type &= 0xf7;

    /* We guarantee a coordinate system that starts in the lower-left corner.
     * Thus, we need to flip  some pictures vertically/horizontally.
     * @BUG: correct??
     */
    if (this->header.image_spec.descriptor & 0x20)
        this->flip_image_vertically();
    if (this->header.image_spec.descriptor & 0x10)
        this->flip_image_horizontally();
    this->header.image_spec.descriptor &= 0xcf;

    TGA::parse_footer(file_ptr);

    fclose(file_ptr);
}

void TGA::read_n_bytes(uint8_t* out, size_t n, const char* name, FILE* file)
{
    size_t ret = fread(out, sizeof(uint8_t), n, file);
    if (ret != n) {
        char msg[4096];
        snprintf(msg, 4095, "expected to read 0x%lx bytes in field `%s', "
                 "got only 0x%lx bytes (%g%%)", n, name, ret,
                 (float)ret / (float)n);
        fail(msg);
    }
}

// The caller must ensure that the read pointer of FILE is at the start of the
// TGA file. After PARSE_HEADER returned, the read pointer will be at the first
// byte _after_ the header. We perform a few checks to ensure that the header
// is well formed).
void TGA::parse_header(FILE* file)
{
    // @NOTE: TGA headers are little-endian, so we don't need to convert ints.
    size_t ret = fread(&this->header, sizeof(this->header), 1, file);
    if (ret != 1) fail("cannot read TGA header from file");

    if (this->header.color_map_type == 0) {
        bool malformed = false;
        malformed |= this->header.color_map_spec.bits_per_pixel != 0;
        malformed |= this->header.color_map_spec.first_entry_index != 0;
        malformed |= this->header.color_map_spec.length != 0;
        if (malformed) fail("malformed TGA header");
    }

    if (this->header.image_spec.width  == 0 ||
        this->header.image_spec.height == 0 ||
        this->header.image_spec.bits_per_pixel == 0)
        fail("in the header, one of image width/height/bpp was set =0");
}

/* @NOTE: Requires a valid buffer of BUF_LEN length to read from. DATA_LEN is
 * the length of the _decoded_ data that we calculated using width, height and
 * pixel depth values from the header.
 */
void TGA::read_rle_image_data(uint8_t* buf, size_t buf_len, size_t data_len)
{
    auto is_rle_packet  { [](uint8_t b) -> bool   { return b & 0x80; } };
    auto get_run_length { [](uint8_t b) -> size_t { return (b & 0x7f) + 1; } };
    size_t bytes_per_pixel = this->get_pixel_width();

    // @BUG: We don't check before reading from BUF _within_ the loop.
    size_t pos = 0;
    while (data_len > 0 && buf_len > 0) {
        bool   parse_rle_packet = is_rle_packet(buf[pos]);
        size_t run_len = get_run_length(buf[pos++]);
        data_len -= run_len * bytes_per_pixel;
        if (parse_rle_packet) {
            while (run_len-- > 0)
                for (size_t i = 0; i < bytes_per_pixel; i++)
                    this->image_data.push_back(buf[pos+i]);
            pos += bytes_per_pixel;
        } else {
            while (run_len-- > 0) {
                for (size_t i = 0; i < bytes_per_pixel; i++)
                    this->image_data.push_back(buf[pos++]);
            }
        }
    }
    assert(data_len == 0 && (pos + sizeof(Footer) == buf_len));
}

/* In case this TGA file doesn't follow the v2 spec, the footer we read is not
 * valid. That fact can be queried via the member IS_NEW_FORMAT. After calling
 * PARSE_FOOTER, the caller can no longer rely on the read pointer of FILE to
 * be at a specific position.
 */
void TGA::parse_footer(FILE* file)
{
    const size_t seek_pos = -26;
    assert(!fseek(file, seek_pos, SEEK_END));

    size_t ret = fread(&this->footer, sizeof(this->footer), 1, file);
    if (ret != 1) fail("cannot read last ", -seek_pos, " bytes from file");

    if (!strncmp(this->footer.signature, "TRUEVISION-XFILE.", 18))
        this->is_new_format = true;
    else
        this->is_new_format = false;

    if (this->footer.dev_dir_offset != 0)
        warn("there is a developer area that we don't parse");
    if (this->footer.ext_area_offset != 0)
        this->parse_ext_area(file);
}

// After calling PARSE_EXT_AREA, the caller can no longer rely on the read
// pointer of FILE to be at a specific position.
void TGA::parse_ext_area(FILE* file)
{
    assert(!fseek(file, this->footer.ext_area_offset, SEEK_SET));
    if (this->footer.ext_area_offset == 0) return;

    fread(&this->ext_area, sizeof(ext_area), 1, file);

    // @NOTE: We aren't using any of the extension area fields.
    if (this->ext_area.color_correction_offset != 0)
        warn("there is a color correction table that we don't parse");
    if (this->ext_area.postage_stamp_offset != 0)
        warn("there is a postage stamp that we don't parse");
    if (this->ext_area.scan_line_tbl_offset != 0)
        warn("there is a scan line table that we don't parse");
}

// @NOTE: The current implementation is extremely wonky:
//  2. We should always write out an extension area.
//  2. We should always write out a developer area?
//  3. We should write a generic accessor to set individual pixels.
//  4. We should validate the correct decoding of different pixel formats.
//  5. We should re-visit the documentation to ensure compliance.
//  6. We shouldn't reset the file pointer too often when reading the input.
//  7. We should probably implement input file parsing outside the ctor.
void TGA::write_to_file(std::string_view filepath)
{
    FILE* outfile = fopen(filepath.data(), "wb");
    if (!outfile) fail("cannot open file `", filepath, '\'');

    assert(
        (this->header.image_spec.height * this->header.image_spec.width *
         this->header.image_spec.bits_per_pixel / 8) ==
        (this->image_data.size())
    );

    auto off = this->get_pixel_width();
    for (size_t row = 0; row < 40; row++) {
        for (size_t col = 0; col < 2000; col += off) {
            size_t pos  = this->get_width() * row + col;
            size_t opos = this->get_width() * (this->get_height()-row) + col;
            for (size_t i = 0; i < off; i++) {
                this->image_data[pos + i] = 0xff;
                if (i == 1) this->image_data[pos + i] = 0x0;

                this->image_data[opos + i] = 0xff;
                if (i == 2) this->image_data[opos + i] = 0x0;
            }
        }
    }
    this->flip_image_horizontally();
    this->flip_image_vertically();

    fwrite(&this->header, sizeof(this->header), 1, outfile);
    fwrite(this->color_map.data(), this->color_map.size(), 1, outfile);
    fwrite(this->image_id_data.data(), this->image_id_data.size(), 1, outfile);
    fwrite(this->image_data.data(), this->image_data.size(), 1, outfile);
    fwrite(&this->footer, sizeof(this->footer), 1, outfile);

    fclose(outfile);
}

void TGA::flip_image_vertically(void)
{
    for (size_t row = 0; row < this->get_height() / 2; row++) {
        size_t flip_row = this->get_height() - row - 1;
        for (size_t col = 0; col < this->get_width(); col++)
            std::swap(
                this->image_data[row      * this->get_width() + col],
                this->image_data[flip_row * this->get_width() + col]
            );
    }
}

void TGA::flip_image_horizontally(void)
{
    /* We need to be careful to not pull apart the bytes of the middle pixel in
     * each line. Thus, we do our calculations on a per-pixel basis and
     * multiply with the byte stride afterwards.
     */
    uint8_t bpp = this->get_pixel_width();
    size_t width_in_pixels = this->header.image_spec.width;
    for (size_t col = 0; col < width_in_pixels / 2; col++) {
        size_t cis_byte_pos   = col * bpp;
        size_t trans_byte_pos = (width_in_pixels - 1 - col) * bpp;
        for (size_t pbyte = 0; pbyte < bpp; pbyte++) {
            size_t ccol = cis_byte_pos + pbyte;
            size_t tcol = trans_byte_pos + pbyte;
            for (size_t row = 0; row < this->get_height(); row++)
                std::swap(
                    this->image_data[row * this->get_width() + ccol],
                    this->image_data[row * this->get_width() + tcol]
                );
        }
    }
}
