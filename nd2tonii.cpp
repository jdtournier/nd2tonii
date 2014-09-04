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
#include <vector>

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
  std::ifstream nd2 (file[0]);
  if (!nd2)
    error ("cannot open input nd2 file \"" + str(file[0]) + "\": " + strerror());


  { // check if output file already exists:
    std::ifstream nii (file[1]);
    if (nii) {
      std::cerr << "WARNING: output file \"" << file[1] << "\" already exists - overwrite (y/N)? ";
      std::string response;
      std::cin >> response;
      if (response != "y" && response != "Y") {
        std::cerr << "aborted\n";
        return 0;
      }
    }
  }

  // open output file:
  std::ofstream nii (file[1], std::ios_base::binary | std::ios_base::trunc);





  // parse input file:

  read_section_header(nd2, 0);

  size_t width (0), height (0);
  int bpp (0), components (0);
  float pixsize (1.0), slice_thickness (1.0);
  std::string description;


  size_t offset = 4096;
  std::vector<size_t> slice_offsets;
  while (nd2.good()) {
    Section section = read_section_header(nd2, offset);
    if (dump_info)
      std::cout << section;
    if (section.name.substr (0, 13) == "ImageDataSeq|") 
      slice_offsets.push_back (section.data);
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
        else if (entry.first == "sDescription")
          description = entry.second;
        else if (entry.first == "dCalibration")
          pixsize = to<float> (entry.second);
        else if (entry.first == "dZStep")
          slice_thickness = to<float> (entry.second);
      }
    }

    offset = section.next;
    offset = 4096 * std::ceil(offset / 4096.0);
  }
  description.resize (80, '\0');

  std::cout << "found " << slice_offsets.size() << " slices of size " << width << " x " << height << std::endl 
    << "  with " << components << " channels and " << bpp << " bits per pixel" << std::endl 
    << "  pixel size: " << pixsize << "um, slice thickness: " << slice_thickness << "um" << std::endl;





  // prepare NIfTI-1 header:
  nifti_1_header H;
  H.sizeof_hdr = 348;
  memcpy (H.data_type, "confocal\0\0\0\0", 10); 
  memcpy (H.db_name, "convert: nd2tonii\0\0", 18);
  H.extents = 16384;
  H.session_error = 0; 
  H.regular = 'r';    
  H.dim_info = 0; 

  H.dim[0] = 4;
  H.dim[1] = width;
  H.dim[2] = height;
  H.dim[3] = slice_offsets.size();
  H.dim[4] = components;
  H.dim[5] = H.dim[6] = H.dim[7] = 0;

  H.intent_p1 = H.intent_p2 = H.intent_p3 = 0.0;
  H.intent_code = NIFTI_INTENT_NONE;
  H.datatype = bpp == 16 ? DT_UINT16 : DT_UINT8;   
  H.bitpix = bpp;    
  H.pixdim[0] = 1.0;
  H.pixdim[1] = H.pixdim[2] = pixsize; H.pixdim[3] = slice_thickness;
  H.pixdim[4] = H.pixdim[5] = H.pixdim[6] = H.pixdim[7] = 0.0;
  H.vox_offset = 352.0;
  H.scl_slope = 1.0;
  H.scl_inter = 0.0;

  H.slice_start = H.slice_end = H.slice_code = 0;
  H.xyzt_units =  NIFTI_UNITS_MM | NIFTI_UNITS_SEC;
  H.cal_max = H.cal_min = H.slice_duration = H.toffset = 0.0;
  H.glmax = H.glmin = 0;

  memcpy (H.descrip, description.c_str(), 80);
  memset (H.aux_file, 0, 24);

  H.qform_code = H.sform_code = 1;
  H.quatern_b = H.quatern_c = H.quatern_d = 0.0;
  H.qoffset_x = H.srow_x[3] = -H.pixdim[1]*H.dim[1]/2.0;
  H.qoffset_y = H.srow_y[3] = -H.pixdim[2]*H.dim[2]/2.0;
  H.qoffset_z = H.srow_z[3] = -H.pixdim[3]*H.dim[3]/2.0;

  H.srow_x[0] = H.srow_y[1] = H.srow_z[2] = 1.0;
  H.srow_x[1] = H.srow_x[2] = H.srow_y[0] = H.srow_y[2] = H.srow_z[0] = H.srow_z[1] = 0.0;

  memcpy (H.intent_name, "confocal microscopy", 16);
  memcpy (H.magic, "n+1\0", 4);



  // write-out:
  nii.write (reinterpret_cast<char*>(&H), sizeof (H));
  nii.write ("\0\0\0\0", 4);

  const size_t numel = width * height;
  uint16_t* in = new uint16_t [numel*components];
  uint16_t* out = new uint16_t [numel];
  for (int current_component = 0; current_component < components; ++current_component) {
    nd2.clear();
    for (size_t n = 0; n < slice_offsets.size(); ++n) {
      nd2.seekg (slice_offsets[n]);
      nd2.read (reinterpret_cast<char*>(in), numel*components*sizeof(uint16_t));
      for (size_t i = 0; i < numel; ++i)
        out[i] = in[components*i+current_component];
      nii.write (reinterpret_cast<char*>(out), numel*sizeof(uint16_t));
    }
  }


}


