/* renderer Copyright (C) 2021 Daniel Schuette
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
#ifndef _TGA_HH_
#define _TGA_HH_

#include <string>
#include <vector>

#include "common.hh"

// Get the byte representation of a word W as a STD::STRING.
template<typename T>
std::string byte_as_str(const T& w)
{
    std::string r { "0b" };
    for (ssize_t i = sizeof(T)*8-1; i >= 0; i--)
        r += (w >> i) ? '1' : '0';
    return r;
}

struct Pixel final {
public:
    uint8_t r = 0, g = 0, b = 0, a = 0xff;

    Pixel(void) = default;
    Pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        : r { r }, g { g }, b { b }, a { a }
    {
    }
};

class TGA {
private:
    // Refer to the spec for a detailed description of these fields.
    struct Header final {
        uint8_t id_length;
        uint8_t color_map_type;
        uint8_t image_type;
        struct ColorMapSpec final {
            uint16_t first_entry_index;
            uint16_t length;
            uint8_t  bits_per_pixel;
        } __attribute__((packed)) color_map_spec;
        struct ImageSpec final {
            uint16_t x_origin, y_origin;
            uint16_t width, height;
            uint8_t  bits_per_pixel; // total # of bits / pixel
            uint8_t  descriptor;     // 0:3 = alpha channel bits, 4:5 = origin
        } __attribute__((packed)) image_spec;
    } __attribute__((packed)) header {};

    /* The footer contains an offset to the developer directory. Since it is
     * mostly application specific, we don't care and don't even parse it when
     * reading a TGA file from disk.
     */
    struct Footer final {
        uint32_t ext_area_offset;
        uint32_t dev_dir_offset;
        char signature[18]; // including a terminating '\0'
    } __attribute__((packed)) footer {};

    struct ExtensionArea final {
        uint16_t length;
        char     author_name[41];     // including a terminating '\0'
        char     author_comment[324]; // 4 strings, each terminated with '\0'
        uint16_t date_time[6];
        char     job_name[41];        // including a terminating '\0'
        uint16_t job_time[3];
        char     software_id[41];     // including a terminating '\0'
        uint16_t software_version0;
        char     software_version1;
        uint8_t  key_color[4];
        uint16_t pixel_aspect_ratio[2];
        uint16_t gamma_value[2];
        uint32_t color_correction_offset;
        uint32_t postage_stamp_offset;
        uint32_t scan_line_tbl_offset;
        uint8_t  attributes_type;
    } __attribute__((packed)) ext_area {};

    bool is_new_format = false;
    std::vector<uint8_t> color_map  {};
    std::vector<uint8_t> image_data {};
    std::vector<uint8_t> image_id_data {};

    // @TODO: not yet implemented.
    // std::vector<uint32_t> scan_line_tbl {};
    // std::vector<uint8_t>  postage_stamp {};
    // std::array<uint16_t, 4096> color_correction_tbl {};

    /* @NOTE: We explicitely _do not_ associate an instance of this class with
     * a particular file for reading/writing. All methods that work on files
     * take either a path or a FILE* as an input parameter.
     */
    void parse_header(FILE*);
    void parse_footer(FILE*);
    void parse_ext_area(FILE*);
    void read_rle_image_data(uint8_t*, size_t, size_t);
    void flip_image_horizontally(void);
    void flip_image_vertically(void);

    static void read_n_bytes(uint8_t*, size_t, const char*, FILE*);

public:
    /* We have two options for working with TGA files:
     *  1. Open an existing file via its path and modify it to our liking
     *  2. Create an TGA file with a desired set of parameters and write
     *     individual pixels into it
     * Both types of TGA files can be flushed to disk, of course.
     */
    explicit TGA(std::string_view);
    explicit TGA(void) = default;

    virtual ~TGA(void) = default;

    void write_to_file(std::string_view);

    /* The width of an individual pixel in bytes. This might _not_ be the same
     * as ``IMAGE_SPEC.BITS_PER_PIXEL / 8'', because pixels can use e.g. just
     * 13 bits instead of using a multiple of 8; they are still written out in
     * byte chunks, though.
     */
    inline uint8_t get_pixel_width(void) const
    {
        size_t bpp = this->header.image_spec.bits_per_pixel;
        return (bpp / 8) + (bpp % 8 == 0 ? 0 : 1);
    }

    // The image width in _bytes_, not pixels.
    inline size_t get_width(void) const
    {
        uint8_t bytes_per_pixel = this->get_pixel_width();
        return this->header.image_spec.width * bytes_per_pixel;
    }

    /* The number of scanlines in the image. Here, bytes vs. pixels do not
     * matter as they do for WIDTH, because pixels cannot span scanlines.
     */
    inline size_t get_height(void) const
    {
        return this->header.image_spec.height;
    }

    // @INCOMPLETE
    inline void set_pixel(size_t, size_t, const Pixel&)
    {
    }
};

#endif /* _TGA_HH_ */
