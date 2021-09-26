/* This is the program entry point.
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
#include "io.hh"
#include "tga.hh"

int main(int argc, char** argv)
{
    if (argc < 2) fail("usage: renderer <tga_input_file>");

    {
        TGA tga_file { *++argv };
        tga_file.write_to_file("outfile0.tga");
    }

    {
        TGA tga_file { 600, 400 };
        for (size_t col = 0; col < 600; col++) {
            tga_file.set_pixel(75, col, { 0, 0xff, 0, 0xff });
            tga_file.set_pixel(150, col, { 0, 0xff, 0, 0xff });
        }
        tga_file.write_to_file("outfile1.tga");
    }

    return 0;
}
