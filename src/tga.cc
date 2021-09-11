#include "tga.hh"
#include "io.hh"

TGA::TGA(std::string_view filepath)
    : TGA {}
{
    FILE* file_ptr = fopen(filepath.data(), "rb");
    if (file_ptr == nullptr) fail("cannot open file `", filepath, '\'');

    // Since TGA headers are little-endian, we don't need to convert ints.
    Header hdr;
    size_t ret = fread(&hdr, sizeof(hdr), 1, file_ptr);
    if (ret != 1) fail("cannot read TGA header from file `", filepath, '\'');

    /* @TODO: Review whether we should actually retain all the data that we do,
     * (or retain things like width/height/...).
     */
    if (hdr.color_map_type == 0x1) {
        this->has_color_map = true;
        this->color_map_spec = hdr.color_map_spec;
    }
    this->set_image_type(hdr);

    /* Lastly, TGA has 3 variable length fields (length follows in parens):
     *  6 = Image ID (ID_LENGTH) -> optional, containing identifying info
     *  7 = Color map (COLOR_MAP_SPEC.LENGTH) -> table containing color map
     *  8 = Image data (IMAGE_SPEC) -> stored according to image descriptor
     */
    uint8_t* bytes = new uint8_t[hdr.id_length];
    ret = fread(bytes, sizeof(uint8_t), hdr.id_length, file_ptr);
    if (ret != hdr.id_length) {
        char msg[4096];
        snprintf(msg, 4095, "expected 0x%x bytes in image id field, "
                 "got only 0x%lx bytes", hdr.id_length, ret);
        fail(msg);
    }

    fclose(file_ptr);
}

/* The IMAGE_TYPE field of the TGA header encodes one of the following
 * properties. We save those via the respective members.
 *  0 = no image data is present
 *  1 = uncompressed color-mapped image
 *  2 = uncompressed true-color image
 *  3 = uncompressed black-and-white (grayscale) image
 *  9 = run-length encoded color-mapped image
 *  10 = run-length encoded true-color image
 *  11 = run-length encoded black-and-white (grayscale) image
 */
void TGA::set_image_type(const TGA::Header& hdr)
{
    if (hdr.image_type != 0)
        this->is_image_present = true;

    if (hdr.image_type >= 9 && hdr.image_type <= 11)
        this->is_rle_compressed = true;
    if (hdr.image_type == 1 || hdr.image_type == 9)
        assert(this->has_color_map &&
               "couldn't find a color map in a color-mapped TGA image");
    if (hdr.image_type == 2 || hdr.image_type == 10)
        this->is_true_color = true;
    if (hdr.image_type == 3 || hdr.image_type == 11)
        this->is_grayscale = true;
}
