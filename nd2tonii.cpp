#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cwchar>
#include <cmath>

#include "nifti1.h"

#define VAR(variable) \
    std::cerr << #variable << " = " << (variable) << " [" << __LINE__ << "]\n"


template <class T> inline std::string str (const T& value)
{
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

template <class T> inline T to (const std::string& string)
{
  std::istringstream stream (string);
  T value;
  stream >> value;
  return value;
}



inline void error (const std::string& message) 
{
  std::cerr << "ERROR: " << message << std::endl;
  exit (1);
}

inline std::string strerror () {
  return strerror (errno);
}




class Section {
  public:
    size_t offset, data, next, unknown;
    std::string name;

    friend std::ostream& operator<< (std::ostream& stream, const Section& s) {
      stream <<"Section: " << s.name <<  ", at offset " << s.offset << ", data at " << s.data << ", next section at " << s.next << std::endl;
      return stream;
    }
};







inline Section read_section_header (std::ifstream& in, size_t offset)
{
  Section section;
  section.offset = offset;

  in.seekg (offset);
  uint32_t u;
  in.read (reinterpret_cast<char*>(&u), sizeof(u));
  if (u != 0x0ABECEDA)
    error ("unexpected word at offset " + str(offset) + " - file not in nd2 format?");

  in.read (reinterpret_cast<char*>(&u), sizeof(u));
  section.data = u;

  in.read (reinterpret_cast<char*>(&u), sizeof(u));
  section.next = u;

  in.read (reinterpret_cast<char*>(&u), sizeof(u));
  section.unknown = u;

  section.data += section.offset + 16;
  section.next += section.data;

  char buffer[1024];
  in.getline (buffer, 1024, '\0');
  section.name = buffer;

  return section;
}


inline std::string read_wide_string (std::ifstream& in) 
{
  char buffer[4096], c = '\0';
  char *p = buffer;
  std::string retval;
  do { 
    in.read (&c, 1);
    *(p++) = c;
    in.seekg(1, std::ios_base::cur);
    if (p-buffer >= 4096) {
      retval += buffer;
      p = buffer;
    }
  } while (c && in.good());
  return retval + buffer;
}



inline std::pair<std::string, std::string> read_entry (std::ifstream& in)
{
  uint8_t n, size;
  in.read (reinterpret_cast<char*> (&n), 1);
  in.read (reinterpret_cast<char*> (&size), 1);

  if (!in.good())
    return std::pair<std::string, std::string>();

  std::string key = read_wide_string (in);
  std::string value;
  std::string type = "unknown";

  switch (n) {
    case 1: type = "bool"; value = str(in.get()); break;
    case 2: type = "int32"; { int32_t i; in.read(reinterpret_cast<char*>(&i),sizeof(int32_t)); value = str(i); } break;
    case 3: type = "uint32"; { uint32_t u; in.read(reinterpret_cast<char*>(&u),sizeof(uint32_t)); value = str(u); } break;
    case 6: type = "double"; { double d; in.read(reinterpret_cast<char*>(&d),sizeof(double)); value = str(d); } break;
    case 11: 
            type = "array uint32"; { 
              uint32_t u;
              do {
                in.read(reinterpret_cast<char*>(&u),sizeof(uint32_t)); 
                value += str(u) + " "; 
              } while (u);
            } break;
    case 8: type = "ws"; value = read_wide_string (in); break;
    default: in.seekg (4, std::ios_base::cur); 
  }


  return std::make_pair (key, value);
}








int main (int argc, char* argv[]) 
{
  if (argc == 1) {
    std::cout << 
      "nd2tonii : convert nd2 images to NIfTI format\n"
      "Written by J-Donald Tournier (jdtournier@gmail.com)\n"
      "usage: nd2tonii [-info] nd2file niftifile\n";
    return 0;
  }

  bool dump_info = false;
  char *file[2];
  char **filep = file;
  for (char** arg = argv+1; *arg; ++arg) {
    if (std::string (*arg) == "-info")
      dump_info = true;
    else 
      *(filep++) = *arg;
  }


  if (filep != file+2) 
    error ("usage: nd2tonii [-info] nd2file niftifile");

  // open input file:
  std::ifstream nd2 (argv[1]);
  if (!nd2)
    error ("cannot open input nd2 file \"" + str(argv[1]) + "\": " + strerror());

/*
  { // check if output file already exists:
    std::ifstream nii (argv[2]);
    if (nii) {
      std::cerr << "WARNING: output file \"" << argv[2] << "\" already exists - overwrite (y/N)? ";
      std::string response;
      std::cin >> response;
      if (response != "y" && response != "Y") {
        std::cerr << "aborting\n";
        return 0;
      }
    }
  }

  // open output file:
  std::ofstream nii (argv[2], std::ios_base::binary | std::ios_base::trunc);
*/
  // parse input file:

  read_section_header(nd2, 0);


  size_t width (0), height (0);
  int bpp (0), components (0);


  size_t offset = 4096;
  size_t slices = 0;
  while (nd2.good()) {
    Section section = read_section_header(nd2, offset);
    if (dump_info)
      std::cout << section;
    if (section.name.substr (0, 13) == "ImageDataSeq|") 
      ++slices;
    else {
      nd2.seekg (section.data);
      while (nd2.tellg() < ssize_t(section.next) && nd2.good()) {
        std::pair<std::string, std::string> entry = read_entry (nd2);
        if (dump_info && ( entry.first.size() || entry.second.size()))
          std::cout << "  " << entry.first << ": " << entry.second << std::endl;

        if (entry.first == "uiTileWidth")
          width = to<size_t> (entry.second);
        else if (entry.first == "uiTileHeight")
          height = to<size_t> (entry.second);
        else if (entry.first == "uiBpcInMemory")
          bpp = to<int> (entry.second);
        else if (entry.first == "uiVirtualComponents")
          components = to<int> (entry.second);
      }
    }

    offset = section.next;
    offset = 4096 * std::ceil(offset / 4096.0);
  }

  VAR (slices);

  std::cout << "found " << slices << " slices of size " << width << " x " << height << ", with " << components << " channels and " << bpp << " bits per pixel" << std::endl;
}


