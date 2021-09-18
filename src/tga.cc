#include <cstring>

#include "tga.hh"
#include "io.hh"

/* @TODO: Review whether we should actually retain all the data that we do,
 * (or retain things like width/height/...).
 * Actually, we should retain header + footer info via our structs because
 * writing out TGA files becomes so much easier. And since we need to keep
 * all image data in core anyways, it's not a lot of overhead. We could of
 * course stream image data in, but why complicate matters?
 */
TGA::TGA(std::string_view filepath)
    : TGA {}
{
    FILE* file_ptr = fopen(filepath.data(), "rb");
    if (file_ptr == nullptr) fail("cannot open file `", filepath, '\'');

    /* TGA is a simple file format with header, footer and variable-sized
     * fields inbetween them. TGA v2.0 defines a developer area and an
     * extension field, but we don't care about those. With warnings enabled,
     * we do print messages if they exist, though.
     */
    TGA::parse_header(file_ptr);
    TGA::parse_footer(file_ptr);

    /* Lastly, TGA has 3 variable length fields (length in parens), that follow
     * right after fixed-sized header:
     *  6 = Image ID (ID_LENGTH) -> optional, containing identifying info
     *  7 = Color map (COLOR_MAP_SPEC.LENGTH) -> table containing color map
     *  8 = Image data (IMAGE_SPEC) -> stored according to image descriptor
     */
    assert(!fseek(file_ptr, sizeof(Header), SEEK_SET));

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
        fail("color-mapped images aren't supported yet");
    }

    if (this->header.color_map_type == 0x0) {
        if ((this->header.image_type & 0x7) == 0x3)
            fail("gray-scale images aren't supported yet");
    }

    size_t bpp = this->header.image_spec.color_bits_per_pixel;
    size_t bytes_per_pixel = (bpp / 8) + (bpp % 8 == 0 ? 0 : 1);
    size_t length = this->header.image_spec.height *
        this->header.image_spec.width * bytes_per_pixel;
    bool is_rle = this->header.image_type & 0x8;
    if (!is_rle) {
        this->image_data.reserve(length);
        TGA::read_n_bytes(this->image_data.data(), length, "image data",
                          file_ptr);
    } else {
        uint8_t* buf = new uint8_t[length];
        size_t count = fread(buf, sizeof(uint8_t), length, file_ptr);
        this->read_rle_image_data(buf, count, length, bytes_per_pixel);
        delete[] buf;
    }

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

template<typename T>
static std::string byte_rep(const T& b)
{
    std::string r { "0b" };
    for (ssize_t i = sizeof(T)*8-1; i >= 0; i--)
        r += (b >> i) ? '1' : '0';
    return r;
}

/* @NOTE: Requires a valid buffer of BUF_LEN length to read from. DATA_LEN is
 * the length of the _decoded_ data that we calculated using width, height and
 * pixel depth values from the header.
 */
void TGA::read_rle_image_data(uint8_t* buf, size_t buf_len, size_t data_len,
                              size_t bytes_per_pixel)
{
    auto is_rle_packet  { [](uint8_t b) -> bool   { return b & 0x80; } };
    auto get_run_length { [](uint8_t b) -> size_t { return (b & 0x7f) + 1; } };

    // @BUG: We don't check before reading from BUF _within_ the loop.
    size_t pos = 0;
    while (data_len > 0 && buf_len > 0) {
        bool parse_rle_packet = is_rle_packet(buf[pos]);
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

/* In case this TGA file doesn't follow the v2 spec, the footer we return is
 * not valid. That fact can be queried via the member IS_NEW_FORMAT.
 * We guarantee that after a call to PARSE_FOOTER, the read pointer of the
 * input FILE will be at the beginning.
 */
void TGA::parse_footer(FILE* file)
{
    const size_t seek_pos = -26;
    assert(!fseek(file, seek_pos, SEEK_END));

    size_t ret = fread(&this->footer, sizeof(this->footer), 1, file);
    if (ret != 1) fail("cannot read last ", -seek_pos, " bytes from file");
    rewind(file);

    if (!strncmp(this->footer.signature, "TRUEVISION-XFILE.", 18))
        this->is_new_format = true;
    else
        this->is_new_format = false;

    if (this->footer.dev_dir_offset != 0)
        warn("there is a developer area that we don't parse");
    if (this->footer.ext_area_offset != 0)
        this->parse_ext_area(file);
}

// We guarantee that the write pointer of FILE is at the start after we return.
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

    rewind(file);
}

/* @NOTE: Since TGA headers are little-endian, we don't need to convert ints.
 * We guarantee that the write pointer of FILE is at the start after we return.
 */
void TGA::parse_header(FILE* file)
{
    size_t ret = fread(&this->header, sizeof(this->header), 1, file);
    if (ret != 1) fail("cannot read TGA header from file");

    if (this->header.color_map_type == 0) {
        bool malformed = false;
        malformed |= this->header.color_map_spec.bits_per_pixel != 0;
        malformed |= this->header.color_map_spec.first_entry_index != 0;
        malformed |= this->header.color_map_spec.length != 0;
        assert(!malformed && "malformed TGA header");
    }
    rewind(file);
}
