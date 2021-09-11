#ifndef _TGA_HH_
#define _TGA_HH_

#include "common.hh"

class TGA {
public:
    // Refer to the spec for a detailed description of these fields.
    struct Header {
        uint8_t id_length;
        uint8_t color_map_type;
        uint8_t image_type;
        struct ColorMapSpec {
            uint16_t first_entry_index;
            uint16_t length;
            uint8_t  bits_per_pixel;
        } color_map_spec __attribute__((packed));
        struct ImageSpec {
            uint16_t x_origin, y_origin;
            uint16_t width, height;
            uint8_t  bits_per_pixel;
            uint8_t  descriptor; // 0:3 -> alpha depth, 4:5 -> direction
        } image_spec __attribute__((packed));
    } __attribute__((packed));

private:
    bool is_image_present  = false;
    bool has_color_map     = false;
    bool is_true_color     = false;
    bool is_grayscale      = false;
    bool is_rle_compressed = false;
    Header::ColorMapSpec color_map_spec {};

    void set_image_type(const Header&);

public:
    /* We have two options for working with TGA files:
     *  1. Open an existing file via its path and modify to our liking
     *  2. Create an TGA file with a desired set of parameters and write
     *     individual pixels into it
     * Both types of TGA files can be flushed to disk, of course.
     */
    explicit TGA(std::string_view);
    explicit TGA(void) = default;

    ~TGA(void) = default;

    bool is_empty_image(void) const { return !this->is_image_present; }
};

#endif /* _TGA_HH_ */
