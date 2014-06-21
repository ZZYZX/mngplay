/*
  mngplay

  $Date: 2001/08/25 22:22:31 $

  Ralph Giles <giles@ashlu.bc.ca>

  This program my be redistributed under the terms of the
  GNU General Public Licence, version 2, or at your preference,
  any later version.

  (this assuming there's no problem with libmng not being GPL...)


  this is an SDL based mng player. the code is very rough;
  patches welcome.
*/

#include <stdio.h>
#include <stdlib.h>
#include <libmng.h>
#include <SDL/SDL.h>


/* structure for keeping track of our mng stream inside the callbacks */
typedef struct {
  FILE    *file;     /* pointer to the file we're decoding */
  char    *filename; /* pointer to the file path/name */
  SDL_Surface *surface;  /* SDL display */
  mng_uint32  delay;     /* ticks to wait before resuming decode */
} mngstuff;

/* callbacks for the mng decoder */

/* memory allocation; data must be zeroed */
mng_ptr mymngalloc(mng_uint32 size)
{
  return (mng_ptr)calloc(1, size);
}

/* memory deallocation */
void mymngfree(mng_ptr p, mng_uint32 size)
{
  free(p);
  return;
}

mng_bool mymngopenstream(mng_handle mng)
{
  mngstuff  *mymng;

  /* look up our stream struct */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* open the file */
  mymng->file = fopen(mymng->filename, "rb");
  if (mymng->file == NULL) {
    fprintf(stderr, "unable to open '%s'\n", mymng->filename);
    return MNG_FALSE;
  }

  return MNG_TRUE;
}

mng_bool mymngclosestream(mng_handle mng)
{
  mngstuff  *mymng;

  /* look up our stream struct */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* close the file */
  fclose(mymng->file);
  mymng->file = NULL; /* for safety */

  return MNG_TRUE;
}

/* feed data to the decoder */
mng_bool mymngreadstream(mng_handle mng, mng_ptr buffer,
    mng_uint32 size, mng_uint32 *bytesread)
{
  mngstuff *mymng;

  /* look up our stream struct */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* read the requested amount of data from the file */
  *bytesread = fread(buffer, 1, size, mymng->file);

  return MNG_TRUE;
}

/* the header's been read. set up the display stuff */
mng_bool mymngprocessheader(mng_handle mng,
    mng_uint32 width, mng_uint32 height)
{
  mngstuff  *mymng;
  SDL_Surface *screen;
  char    title[256];

//  fprintf(stderr, "our mng is %dx%d\n", width,height);

  screen = SDL_SetVideoMode(width,height, 32, SDL_SWSURFACE);
  if (screen == NULL) {
    fprintf(stderr, "unable to allocate %dx%d video memory: %s\n", 
      width, height, SDL_GetError());
    return MNG_FALSE;
  }

  /* save the surface pointer */
  mymng = (mngstuff*)mng_get_userdata(mng);
  mymng->surface = screen;

  /* set a descriptive window title */
  snprintf(title, 256, "mngplay: %s", mymng->filename);
  SDL_WM_SetCaption(title, "mngplay");

  /* if necessary, lock the drawing surface so the decoder
     can safely fill it. We'll unlock elsewhere before display */
  if (SDL_MUSTLOCK(mymng->surface)) {
    if ( SDL_LockSurface(mymng->surface) < 0 ) {
      fprintf(stderr, "could not lock display surface\n");
      exit(1);
    }
  }

  /* tell the mng decoder about our bit-depth choice */
  /* FIXME: SDL wants BGRA on intel, ARGB on ppc. should detect */
  mng_set_canvasstyle(mng, MNG_CANVAS_BGRA8);
  mng_set_canvasstyle(mng, MNG_CANVAS_ARGB8);

  return MNG_TRUE;
}

/* return a row pointer for the decoder to fill */
mng_ptr mymnggetcanvasline(mng_handle mng, mng_uint32 line)
{
  mngstuff  *mymng;
  SDL_Surface *surface;
  mng_ptr   row;

  /* dereference our structure */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* we assume any necessary locking has happened 
     outside, in the frame level code */
  row = mymng->surface->pixels + mymng->surface->pitch*line;

  return (row); 
}

/* timer */
mng_uint32 mymnggetticks(mng_handle mng)
{
  return (mng_uint32)SDL_GetTicks();
}

mng_bool mymngrefresh(mng_handle mng, mng_uint32 x, mng_uint32 y,
      mng_uint32 w, mng_uint32 h)
{
  mngstuff  *mymng;

  /* dereference our structure */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* if necessary, unlock the display */
  if (SDL_MUSTLOCK(mymng->surface)) {
    SDL_UnlockSurface(mymng->surface);
  }

  /* refresh the screen with the new frame */
  SDL_UpdateRect(mymng->surface, x,y, w,h);
  
  /* in necessary, relock the drawing surface */
  if (SDL_MUSTLOCK(mymng->surface)) {
    if ( SDL_LockSurface(mymng->surface) < 0 ) {
      fprintf(stderr, "could not lock display surface\n");
      return MNG_FALSE;
    }
  }
  

  return MNG_TRUE;
}

/* interframe delay callback */
mng_bool mymngsettimer(mng_handle mng, mng_uint32 msecs)
{
  mngstuff  *mymng;

//  fprintf(stderr,"  pausing for %d ms\n", msecs);
  
  /* look up our stream struct */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* set the timer for when the decoder wants to be woken */
  mymng->delay = msecs;

  return MNG_TRUE;
  
}

mng_bool mymngerror(mng_handle mng, mng_int32 code, mng_int8 severity,
  mng_chunkid chunktype, mng_uint32 chunkseq,
  mng_int32 extra1, mng_int32 extra2, mng_pchar text)
{
  mngstuff  *mymng;
  char    chunk[5];
  
  /* dereference our data so we can get the filename */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* pull out the chuck type as a string */
  // FIXME: does this assume unsigned char?
  chunk[0] = (char)((chunktype >> 24) & 0xFF);
  chunk[1] = (char)((chunktype >> 16) & 0xFF);
  chunk[2] = (char)((chunktype >>  8) & 0xFF);
  chunk[3] = (char)((chunktype      ) & 0xFF);
  chunk[4] = '\0';

  /* output the error */
  fprintf(stderr, "error playing '%s' chunk %s (%d):\n",
    mymng->filename, chunk, chunkseq);
  fprintf(stderr, "%s\n", text);

  return (0);
}

int mymngquit(mng_handle mng)
{
  mngstuff  *mymng;

  /* dereference our data so we can free it */
  mymng = (mngstuff*)mng_get_userdata(mng);

  /* cleanup. this will call mymngclosestream */
  mng_cleanup(&mng);

  /* free our data */
  free(mymng);
  
  /* quit */
  exit(0);
}

int checkevents(mng_handle mng)
{
  SDL_Event event;

  /* check if there's an event pending */
  if (!SDL_PollEvent(&event)) {
    return 0; /* no events pending */
  }

  /* we have an event; process it */
  switch (event.type) {
    case SDL_QUIT:
      mymngquit(mng); /* quit */ 
      break;
    case SDL_KEYUP:
      switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
        case SDLK_q:
          mymngquit(mng);
          break;
      }
    default:
      return 1;
  }
}

int main(int argc, char *argv[])
{
  mngstuff  *mymng;
  mng_handle  mng;
  SDL_Rect  updaterect;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <mngfile>\n", argv[0]);
    exit(1);
  }

  /* allocate our stream data structure */
  mymng = (mngstuff*)calloc(1, sizeof(*mymng));
  if (mymng == NULL) {
    fprintf(stderr, "could not allocate stream structure.\n");
    exit(0);
  }

  /* pass the name of the file we want to play */
  mymng->filename = argv[1];

  /* set up the mng decoder for our stream */
  mng = mng_initialize(mymng, mymngalloc, mymngfree, MNG_NULL);
  if (mng == MNG_NULL) {
    fprintf(stderr, "could not initialize libmng.\n");
    exit(1);
  }

  /* set the callbacks */
  mng_setcb_errorproc(mng, mymngerror);
  mng_setcb_openstream(mng, mymngopenstream);
  mng_setcb_closestream(mng, mymngclosestream);
  mng_setcb_readdata(mng, mymngreadstream);
  mng_setcb_gettickcount(mng, mymnggetticks);
  mng_setcb_settimer(mng, mymngsettimer);
  mng_setcb_processheader(mng, mymngprocessheader);
  mng_setcb_getcanvasline(mng, mymnggetcanvasline);
  mng_setcb_refresh(mng, mymngrefresh);
  /* FIXME: should check for errors here */

  /* initialize SDL */
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "%s: Unable to initialize SDL (%s)\n",
      argv[0], SDL_GetError());
    exit(1);
  }
  /* arrange to call the shutdown routine before we exit */
  atexit(SDL_Quit);

  /* restrict event handling to the relevant bits */
  SDL_EventState(SDL_KEYDOWN, SDL_IGNORE); /* keyup only */
  SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
  SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_IGNORE);
  SDL_EventState(SDL_MOUSEBUTTONUP, SDL_IGNORE);

//  fprintf(stderr, "playing mng...maybe.\n");

  mng_readdisplay(mng);

  /* loop though the frames */
  while (mymng->delay) {
//    fprintf(stderr, "  waiting for %d ms\n", mymng->delay);
    SDL_Delay(mymng->delay);

    /* reset the delay in case the decoder
       doesn't update it again */
    mymng->delay = 0;

    mng_display_resume(mng);

    /* check for user input (just quit at this point) */
    checkevents(mng);
  }

  /* cleanup and quit */
  mymngquit(mng);
}

