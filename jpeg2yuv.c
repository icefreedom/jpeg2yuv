/*
jpeg2yuv
========

  Converts a collection of JPEG images to a YUV4MPEG stream.
  (see jpeg2yuv -h for help (or have a look at the function "usage"))
  
  Copyright (C) 1999 Gernot Ziegler (gz@lysator.liu.se)
  Copyright (C) 2001 Matthew Marjanovic (maddog@mir.com)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string.h>
#include <errno.h>
#include <jpeglib.h>
#include "jpegutils.h"
#include "lav_io.h"

#include <sys/types.h>
#include <dirent.h>

#include "mjpeg_logging.h"
#include "mjpeg_types.h"

#include "yuv4mpeg.h"
#include "mpegconsts.h"

#define MAXPIXELS (2048*2048)  /* Maximum size of final image */



typedef struct _parameters {
  char *jpegformatstr;
  uint32_t begin;       /* the video frame start */
  int32_t numframes;   /* -1 means: take all frames */
  y4m_ratio_t framerate;
  int interlace;   /* will the YUV4MPEG stream be interlaced? */
  int interleave;  /* are the JPEG frames field-interleaved? */
  int verbose; /* the verbosity of the program (see mjpeg_logging.h) */

  int width;
  int height;
  int colorspace;
  int loop;
  int rescale_YUV;
} parameters_t;


static struct jpeg_decompress_struct dinfo;
static struct jpeg_error_mgr jerr;



/*
 * The User Interface parts 
 */

/* usage
 * Prints a short description of the program, including default values 
 * in: prog: The name of the program 
 */
static void usage(char *prog)
{
  char *h;
  
  if (NULL != (h = (char *)strrchr(prog,'/')))
    prog = h+1;
  
  fprintf(stderr, 
      "usage: %s [ options ]\n"
      "\n"
      "where options are ([] shows the defaults):\n"
      "  -l num        loop -1=forever, n >= 1 n-times       \n"
      "  -v num        verbosity (0,1,2)                  [1]\n"
      "  -b framenum   starting frame number              [0]\n"
      "  -f framerate  framerate for output stream (fps)     \n"
      "  -n numframes  number of frames to process        [-1 = all]\n"
      "  -j {1}%%{2}d{3} Read JPEG frames with the name components as follows:\n"
      "               {1} JPEG filename prefix (e g rendered_ )\n"
      "               {2} Counting placeholder (like in C, printf, eg 06 ))\n"
      "  -I x  interlacing mode:  p = none/progressive\n"
      "                           t = top-field-first\n"
      "                           b = bottom-field-first\n"
      "  -L x  interleaving mode:  0 = non-interleaved (two successive\n"
      "                                 fields per JPEG file)\n"
      "                            1 = interleaved fields\n"
      "  -R 1/0 ... 1: rescale YUV color values from 0-255 to 16-235 (default: 1)\n"
      "\n"
      "%s pipes a sequence of JPEG files to stdout,\n"
      "making the direct encoding of MPEG files possible under mpeg2enc.\n"
      "Any JPEG format supported by libjpeg can be read.\n"
      "stdout will be filled with the YUV4MPEG movie data stream,\n"
      "so be prepared to pipe it on to mpeg2enc or to write it into a file.\n"
      "\n"
      "\n"
      "examples:\n"
      "  %s -j in_%%06d.jpeg -b 100000 > result.yuv\n"
      "  | combines all the available JPEGs that match \n"
      "    in_??????.jpeg, starting with 100000 (in_100000.jpeg, \n"
      "    in_100001.jpeg, etc...) into the uncompressed YUV4MPEG videofile result.yuv\n"
      "  %s -It -L0 -j abc_%%04d.jpeg | mpeg2enc -f3 -o out.m2v\n"
      "  | combines all the available JPEGs that match \n"
      "    abc_??????.jpeg, starting with 0000 (abc_0000.jpeg, \n"
      "    abc_0001.jpeg, etc...) and pipes it to mpeg2enc which encodes\n"
      "    an MPEG2-file called out.m2v out of it\n"
      "\n",
      prog, prog, prog, prog);
}



/* parse_commandline
 * Parses the commandline for the supplied parameters.
 * in: argc, argv: the classic commandline parameters
 */
static void parse_commandline(int argc, char ** argv, parameters_t *param)
{
  int c;
  
  param->jpegformatstr = NULL;
  param->begin = 0;
  param->numframes = -1;
  param->framerate = y4m_fps_UNKNOWN;
  param->interlace = Y4M_UNKNOWN;
  param->interleave = -1;
  param->verbose = 1;
  param->loop = 1;
  param->rescale_YUV = 1;

  /* parse options */
  for (;;) {
    if (-1 == (c = getopt(argc, argv, "I:hv:L:b:j:n:f:l:R:")))
      break;
    switch (c) {

    case 'j':
      param->jpegformatstr = strdup(optarg);
      break;
    case 'b':
      param->begin = atol(optarg);
      break;
    case 'n':
      param->numframes = atol(optarg);
      break;
    case 'R':
      param->rescale_YUV = atoi(optarg);
      break;
    case 'f':
      param->framerate = mpeg_conform_framerate(atof(optarg));
      break;
    case 'I':
      switch (optarg[0]) 
    {
    case 'p':
      param->interlace = Y4M_ILACE_NONE;
      break;
    case 't':
      param->interlace = Y4M_ILACE_TOP_FIRST;
      break;
    case 'b':
      param->interlace = Y4M_ILACE_BOTTOM_FIRST;
      break;
    default:
      mjpeg_error_exit1 ("-I option requires arg p, t, or b");
    }
      break;
    case 'L':
      param->interleave = atoi(optarg);
      if ((param->interleave != 0) &&
      (param->interleave != 1)) 
    mjpeg_error_exit1 ("-L option requires arg 0 or 1");
      break;
    case 'v':
      param->verbose = atoi(optarg);
      if (param->verbose < 0 || param->verbose > 2) 
    mjpeg_error_exit1( "-v option requires arg 0, 1, or 2");    
      break;     
    case 'l':
      param->loop = atoi(optarg);
      if  (param->loop == 0 || param->loop < -1 ) 
    mjpeg_error_exit1( "-l option requires a number greater than 0 or -1 to loop forever ");    
      break;     
    case 'h':
    default:
      mjpeg_info("Wp x, char %c\n", c);

      usage(argv[0]);
      exit(1);
    }
  }
  if (param->jpegformatstr == NULL) { 
    mjpeg_error("%s:  input format string not specified. (Use -j option.)",
        argv[0]); 
    usage(argv[0]); 
    exit(1);
  }
  if (Y4M_RATIO_EQL(param->framerate, y4m_fps_UNKNOWN)) {
    mjpeg_error("%s:  framerate not specified.  (Use -f option)",
        argv[0]); 
    usage(argv[0]); 
    exit(1);
  }
}


/*
 * The file handling parts 
 */

/** init_parse_files
 * Verifies the JPEG input files and prepares YUV4MPEG header information.
 * @returns 0 on success
 */
static int init_parse_files(parameters_t *param, char *filename)
{ 
  char jpegname[255];
  FILE *jpegfile;

/*  
  snprintf(jpegname, sizeof(jpegname), 
       param->jpegformatstr, param->begin);
*/
  strcpy(jpegname, filename);
  mjpeg_info("Parsing file %s", jpegname);

  mjpeg_debug("Analyzing %s to get the right pic params", jpegname);
  jpegfile = fopen(jpegname, "rb");
  
  if (jpegfile == NULL) {
    mjpeg_error_exit1("System error while opening: \"%s\": %s",
              jpegname, strerror(errno));
    return 1;
  }

  /* Now open this JPEG file, and examine its header to retrieve the 
     YUV4MPEG info that shall be written */
  dinfo.err = jpeg_std_error(&jerr);  /* ?????????? */
  jpeg_create_decompress(&dinfo);
  jpeg_stdio_src(&dinfo, jpegfile);
  jpeg_read_header(&dinfo, 1);
  switch (dinfo.jpeg_color_space)
    {
    case JCS_YCbCr:
      mjpeg_info("YUV colorspace detected.\n"); 
      dinfo.out_color_space = JCS_YCbCr;      
      break;
    case JCS_GRAYSCALE:
      mjpeg_info("Grayscale colorspace detected.\n"); 
      dinfo.out_color_space = JCS_GRAYSCALE;      
      break;
    default:
      mjpeg_error("Unsupported colorspace detected.\n"); break;
    }

  mjpeg_info("Starting decompression");

  jpeg_start_decompress(&dinfo);
  
  if (dinfo.output_components != 3 && dinfo.out_color_space == JCS_YCbCr)
    mjpeg_error_exit1("Output components of color JPEG image = %d, must be 3.",
              dinfo.output_components);

  if (dinfo.output_components != 1 && dinfo.out_color_space == JCS_GRAYSCALE)
    mjpeg_error_exit1("Output components of grayscale JPEG image = %d, must be 1.",
              dinfo.output_components);
  
  mjpeg_info("Image dimensions are %dx%d",
         dinfo.image_width, dinfo.image_height);
  /* picture size check  */
  if ( (dinfo.image_width % 2) != 0 )
    mjpeg_error_exit1("The image width has to be a even number, rescale the image");
  if ( (dinfo.image_height % 2) != 0 )
    mjpeg_error_exit1("The image height has to be even number, rescale the image");

  param->width = dinfo.image_width;
  param->height = dinfo.image_height;
  param->colorspace = dinfo.jpeg_color_space;
  
  jpeg_destroy_decompress(&dinfo);
  fclose(jpegfile);

  mjpeg_info("Movie frame rate is:  %f frames/second",
         Y4M_RATIO_DBL(param->framerate));

  switch (param->interlace) {
  case Y4M_ILACE_NONE:
    mjpeg_info("Non-interlaced/progressive frames.");
    break;
  case Y4M_ILACE_BOTTOM_FIRST:
    mjpeg_info("Interlaced frames, bottom field first.");      
    break;
  case Y4M_ILACE_TOP_FIRST:
    mjpeg_info("Interlaced frames, top field first.");      
    break;
  default:
    mjpeg_error_exit1("Interlace has not been specified (use -I option)");
    break;
  }

  if ((param->interlace != Y4M_ILACE_NONE) && (param->interleave == -1))
    mjpeg_error_exit1("Interleave has not been specified (use -L option)");

  if (!(param->interleave) && (param->interlace != Y4M_ILACE_NONE)) {
    param->height *= 2;
    mjpeg_info("Non-interleaved fields (image height doubled)");
  }
  mjpeg_info("Frame size:  %d x %d", param->width, param->height);

  return 0;
}

/**
  Rescales the YUV values from the range 0..255 to the range 16..235 
  @param yp: buffer for Y plane of decoded JPEG 
  @param up: buffer for U plane of decoded JPEG 
  @param vp: buffer for V plane of decoded JPEG 
*/
static void rescale_color_vals(int width, int height, uint8_t *yp, uint8_t *up, uint8_t *vp) 
{
  int x,y;
  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      yp[x+y*width] = (float)(yp[x+y*width])/255.0 * (235.0 - 16.0) + 16.0;

  for (y = 0; y < height/2; y++)
    for (x = 0; x < width/2; x++)
      {
    up[x+y*width/2] = (float)(up[x+y*width/2])/255.0 * (240.0 - 16.0) + 16.0;
    vp[x+y*width/2] = (float)(vp[x+y*width/2])/255.0 * (240.0 - 16.0) + 16.0;
      }
}

static int generate_YUV4MPEG(parameters_t *param)
{
  uint32_t frame;
  size_t jpegsize;
  char jpegname[FILENAME_MAX];
  FILE *jpegfile;
  int loops;                                 /* number of loops to go */
  uint8_t *yuv[3];  /* buffer for Y/U/V planes of decoded JPEG */
  static uint8_t jpegdata[MAXPIXELS];  /* that ought to be enough */
  y4m_stream_info_t streaminfo;
  y4m_frame_info_t frameinfo;
  loops = param->loop;
  DIR *dirp;
  struct dirent *dp;

  yuv[0] = yuv[1] 
     = yuv[2] 
     = NULL;

  mjpeg_info("Number of Loops %i", loops);

  mjpeg_info("Now generating YUV4MPEG stream.");

  dirp = opendir(param->jpegformatstr);
  if (dirp == NULL) {
           mjpeg_info("Could not open input directory.");
       return 1;
  } else {
           mjpeg_info("Opening input directory.");
  }
 
     while ((dp = readdir(dirp)) != NULL)
     {
    sprintf(jpegname, "%s%s", param->jpegformatstr, dp->d_name);

    if (!strstr(dp->d_name, ".jpg") && !strstr(dp->d_name, ".JPG") && !strstr(dp->d_name, ".jpeg") && !strstr(dp->d_name, ".JPEG"))
        continue;

    if (init_parse_files(param, jpegname))
        continue;

    y4m_init_stream_info(&streaminfo);
    y4m_init_frame_info(&frameinfo);

    y4m_si_set_width(&streaminfo, param->width);
    y4m_si_set_height(&streaminfo, param->height);
    y4m_si_set_interlace(&streaminfo, param->interlace);
    y4m_si_set_framerate(&streaminfo, param->framerate);

    yuv[0] = realloc(yuv[0], param->width * param->height * sizeof(yuv[0][0]));
    yuv[1] = realloc(yuv[1], param->width * param->height / 4 * sizeof(yuv[1][0]));
    yuv[2] = realloc(yuv[2], param->width * param->height / 4 * sizeof(yuv[2][0]));

    y4m_write_stream_header(STDOUT_FILENO, &streaminfo);

   
//       snprintf(jpegname, sizeof(jpegname), param->jpegformatstr, frame);
       jpegfile = fopen(jpegname, "rb");
       
       if (jpegfile == NULL) { 
         mjpeg_info("Read from '%s' failed:  %s", dp->d_name, strerror(errno));
         if (param->numframes == -1) {
           mjpeg_info("No more frames.  Stopping.");
           break;  /* we are done; leave 'while' loop */
         } else {
           mjpeg_info("Rewriting latest frame instead.");
         }
       } else {
         mjpeg_debug("Preparing frame");
         
         jpegsize = fread(jpegdata, sizeof(unsigned char), MAXPIXELS, jpegfile); 
         fclose(jpegfile);
         
         /* decode_jpeg_raw:s parameters from 20010826
          * jpeg_data:       buffer with input / output jpeg
          * len:             Length of jpeg buffer
          * itype:           0: Interleaved/Progressive
          *                  1: Not-interleaved, Top field first
          *                  2: Not-interleaved, Bottom field first
          * ctype            Chroma format for decompression.
          *                  Currently always 420 and hence ignored.
          * raw0             buffer with input / output raw Y channel
          * raw1             buffer with input / output raw U/Cb channel
          * raw2             buffer with input / output raw V/Cr channel
          * width            width of Y channel (width of U/V is width/2)
          * height           height of Y channel (height of U/V is height/2)
          */
   
         if ((param->interlace == Y4M_ILACE_NONE) || (param->interleave == 1)) {
           mjpeg_info("Processing non-interlaced/interleaved %s, size %ul.", 
                      dp->d_name, jpegsize);
       if (param->colorspace == JCS_GRAYSCALE)
           decode_jpeg_gray_raw(jpegdata, jpegsize,
                    0, 420, param->width, param->height,
                    yuv[0], yuv[1], yuv[2]);
       else
         decode_jpeg_raw(jpegdata, jpegsize,
                 0, 420, param->width, param->height,
                 yuv[0], yuv[1], yuv[2]);
         } else {
           switch (param->interlace) {
           case Y4M_ILACE_TOP_FIRST:
             mjpeg_info("Processing interlaced, top-first %s, size %ul.",
                        jpegname, jpegsize);
         if (param->colorspace == JCS_GRAYSCALE)
           decode_jpeg_gray_raw(jpegdata, jpegsize,
                    LAV_INTER_TOP_FIRST, 
                    420, param->width, param->height,
                    yuv[0], yuv[1], yuv[2]);
         else
           decode_jpeg_raw(jpegdata, jpegsize,
                   LAV_INTER_TOP_FIRST,
                   420, param->width, param->height,
                   yuv[0], yuv[1], yuv[2]);
             break;
           case Y4M_ILACE_BOTTOM_FIRST:
             mjpeg_info("Processing interlaced, bottom-first %s, size %ul.", 
                        jpegname, jpegsize);
         if (param->colorspace == JCS_GRAYSCALE)
           decode_jpeg_gray_raw(jpegdata, jpegsize,
                    LAV_INTER_BOTTOM_FIRST, 
                    420, param->width, param->height,
                    yuv[0], yuv[1], yuv[2]);
         else
           decode_jpeg_raw(jpegdata, jpegsize,
                   LAV_INTER_BOTTOM_FIRST,
                   420, param->width, param->height,
                   yuv[0], yuv[1], yuv[2]);
             break;
           default:
             mjpeg_error_exit1("FATAL logic error?!?");
             break;
           }
         }

     if (param->rescale_YUV)
       {
         mjpeg_info("Rescaling color values.");
         rescale_color_vals(param->width, param->height, yuv[0], yuv[1], yuv[2]);
       }
     mjpeg_debug("Frame decoded, now writing to output stream.");
       }
   
  loops = param->loop;
  do { /* while */
       y4m_write_frame(STDOUT_FILENO, &streaminfo, &frameinfo, yuv);
     if (param->loop != -1)
       loops--;
 
  } while( loops >=1 || loops == -1 );

  } /* while */
  
  y4m_fini_stream_info(&streaminfo);
  y4m_fini_frame_info(&frameinfo);

  closedir(dirp);

  free(yuv[0]);
  free(yuv[1]);
  free(yuv[2]);

  return 0;
}



/* main
 * in: argc, argv:  Classic commandline parameters. 
 * returns: int: 0: success, !0: !success :-)
 */
int main(int argc, char ** argv)
{ 
  parameters_t param;

  parse_commandline(argc, argv, &param);
  mjpeg_default_handler_verbosity(param.verbose);


/*
  mjpeg_info("Parsing & checking input files.");
  if (init_parse_files(&param)) {
    mjpeg_error_exit1("* Error processing the JPEG input.");
  }
*/

  if (generate_YUV4MPEG(&param)) { 
    mjpeg_error_exit1("* Error processing the input files.");
  }

  return 0;
}

