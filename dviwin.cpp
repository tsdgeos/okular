//
// Class: dviWindow
//
// Previewer for TeX DVI files.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>

#include <qpainter.h>
#include <qbitmap.h> 
#include <qkeycode.h>
#include <qpaintdevice.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qurl.h>

#include <kapp.h>
#include <kmessagebox.h>
#include <kdebug.h>
#include <klocale.h>
#include <kprocess.h>

#include "dviwin.h"
#include "optiondialog.h"


//------ some definitions from xdvi ----------


#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#define	MAXDIM		32767

struct WindowRec mane	= {(Window) 0, 3, 0, 0, 0, 0, MAXDIM, 0, MAXDIM, 0};
struct WindowRec currwin = {(Window) 0, 3, 0, 0, 0, 0, MAXDIM, 0, MAXDIM, 0};
extern	struct WindowRec alt;

struct drawinf	currinf;
//int	_debug;
char	*prog;
Display	*DISP;
//int		min_x, max_x, min_y, max_y;
struct font	*font_head = NULL;
char	*dvi_name = NULL;
int	total_pages;
int	current_page;
FILE	*dvi_file;			/* user's file */
const char *dvi_oops_msg;	/* error message */
double	dimconv;
int	n_files_left;	/* for LRU closing of fonts */
jmp_buf	dvi_env;	/* mechanism to communicate dvi file errors */
long	*page_offset;

QIntDict<struct font> tn_table;

unsigned short	current_timestamp = 0;
Screen	*SCRN;


#include "c-openmx.h" // for OPEN_MAX

int	_pixels_per_inch;
_Xconst char	*_paper;



// The following are really used
long	        magnification;
unsigned int	page_w;
unsigned int	page_h;
// end of "really used"

extern char *           prog;
extern char *	        dvi_name;
extern FILE *		dvi_file;
extern int 		n_files_left;
//extern int 		min_x;
//extern int 		min_y;
//extern int 		max_x;
//extern int 		max_y;
extern unsigned int	page_w, page_h;
extern int 		current_page;
extern int 		total_pages;
extern Display *	DISP;
extern Screen  *	SCRN;
Window                  mainwin;

void 	draw_page(void);
extern "C" void 	kpse_set_progname(const char*);
extern Boolean check_dvi_file(void);
void 	reset_fonts();
extern "C" {
#undef PACKAGE // defined by both c-auto.h and config.h
#undef VERSION
#include <kpathsea/c-auto.h>
#include <kpathsea/paths.h>
#include <kpathsea/proginit.h>
#include <kpathsea/tex-file.h>
#include <kpathsea/tex-glyph.h>
}
#include <setjmp.h>
extern	jmp_buf	dvi_env;	/* mechanism to communicate dvi file errors */
extern	QDateTime dvi_time;



double xres;

//------ next the drawing functions called from C-code (dvi_draw.c) ----

QPainter foreGroundPaint; // QPainter used for text


extern  void qt_processEvents(void)
{
	qApp->processEvents();
}


//------ now comes the dviWindow class implementation ----------

dviWindow::dviWindow( int bdpi, double zoom, const char *mfm, int mkpk, QWidget *parent, const char *name ) 
  : QWidget( parent, name )
{
  setBackgroundMode(NoBackground);

  ChangesPossible = 1;
  FontPath = QString::null;
  setFocusPolicy(QWidget::StrongFocus);
  setFocus();
  
  // initialize the dvi machinery

  setResolution( bdpi );
  setMakePK( mkpk );
  setMetafontMode( mfm );
  unshrunk_paper_w       = int( 21.0 * basedpi/2.54 + 0.5 ); // set A4 paper as default
  unshrunk_paper_h       = int( 27.9 * basedpi/2.54 + 0.5 ); 
  PostScriptDirectory    = NULL;
  PostScriptOutPutString = NULL;
  HTML_href              = NULL;
  DISP                   = x11Display();
  SCRN                   = DefaultScreenOfDisplay(DISP);
  mainwin                = handle();
  mane                   = currwin;
  _postscript            = 0;
  pixmap                 = NULL;
  xres                   = ((double)(DisplayWidth(DISP,(int)DefaultScreen(DISP)) *25.4) /
			    DisplayWidthMM(DISP,(int)DefaultScreen(DISP)) );
  mane.shrinkfactor      = currwin.shrinkfactor = (double)basedpi/(xres*zoom);
  _zoom                  = zoom;

  resize(0,0);
}

dviWindow::~dviWindow()
{
  ;
}

void dviWindow::setShowPS( int flag )
{
  kdDebug() << "setShowPS" << endl;

  if ( _postscript == flag )
    return;
  _postscript = flag;
  drawPage();
}

int dviWindow::showPS()
{
  return _postscript;
}

void dviWindow::setShowHyperLinks( int flag )
{
  if ( _showHyperLinks == flag )
    return;
  _showHyperLinks = flag;

  drawPage();
}

int dviWindow::showHyperLinks()
{
  return _showHyperLinks;
}

void dviWindow::setMakePK( int flag )
{
  if (!ChangesPossible)
    KMessageBox::sorry( this,
			i18n("The change in font generation will be effective\n"
			     "only after you start kdvi again!") );
  makepk = flag;
}

int dviWindow::makePK()
{
	return makepk;	
}
	
void dviWindow::setFontPath( const char *s )
{
  if (!ChangesPossible)
    KMessageBox::sorry( this,
			i18n("The change in font path will be effective\n"
			     "only after you start kdvi again!"));
  FontPath = s;
}

const char * dviWindow::fontPath()
{
  return FontPath;
}

void dviWindow::setMetafontMode( const char *mfm )
{
  if (!ChangesPossible)
    KMessageBox::sorry( this,
			i18n("The change in Metafont mode will be effective\n"
			     "only after you start kdvi again!") );
  MetafontMode = mfm;
}

const char * dviWindow::metafontMode()
{
  return MetafontMode;
}


void dviWindow::setPaper(double w, double h)
{
  unshrunk_paper_w = int( w * basedpi/2.54 + 0.5 );
  unshrunk_paper_h = int( h * basedpi/2.54 + 0.5 ); 
  unshrunk_page_w  = unshrunk_paper_w;
  unshrunk_page_h  = unshrunk_paper_h;
  page_w           = (int)(unshrunk_page_w / mane.shrinkfactor  + 0.5) + 2;
  page_h           = (int)(unshrunk_page_h / mane.shrinkfactor  + 0.5) + 2;
  reset_fonts();
  changePageSize();
}


void dviWindow::setResolution( int bdpi )
{
  if (!ChangesPossible)
    KMessageBox::sorry( this,
			i18n("The change in resolution will be effective\n"
			     "only after you start kdvi again!") );
  basedpi          = bdpi;
  _pixels_per_inch = bdpi;
}

int dviWindow::resolution()
{
  return basedpi;
}


void dviWindow::initDVI()
{
        prog = const_cast<char*>("kdvi");
	n_files_left = OPEN_MAX;
	kpse_set_progname ("xdvi");
	kpse_init_prog ("XDVI", basedpi, MetafontMode.data(), "cmr10");
	kpse_set_program_enabled(kpse_any_glyph_format,
				 makepk, kpse_src_client_cnf);
	kpse_format_info[kpse_pk_format].override_path
		= kpse_format_info[kpse_gf_format].override_path
		= kpse_format_info[kpse_any_glyph_format].override_path
		= kpse_format_info[kpse_tfm_format].override_path
		= FontPath.ascii();
	ChangesPossible = 0;
}


//------ this function calls the dvi interpreter ----------

void dviWindow::drawPage()
{
  if (filename.isEmpty()) {	// must call setFile first
    resize(0, 0);
    return;
  }

  if (!dvi_name) {			//  dvi file not initialized yet
    QApplication::setOverrideCursor( waitCursor );
    dvi_name = const_cast<char*>(filename.ascii());

    dvi_file = NULL;
    if (setjmp(dvi_env)) {	// dvi_oops called
      dvi_time.setTime_t(0); // force init_dvi_file
      QApplication::restoreOverrideCursor();
      KMessageBox::error( this,
			  i18n("File corruption!\n\n") +
			  dvi_oops_msg +
			  i18n("\n\nMost likely this means that the DVI file\n") + 
			  filename +
			  i18n("\nis broken, or that it is not a DVI file."));
      return;
    }
    check_dvi_file();
    QApplication::restoreOverrideCursor();
    gotoPage(1);
    changePageSize();
    return;
  }

  if ( !pixmap )
    return;

  if ( !pixmap->paintingActive() ) {

    foreGroundPaint.begin( pixmap );
    QApplication::setOverrideCursor( waitCursor );
    if (setjmp(dvi_env)) {	// dvi_oops called
      dvi_time.setTime_t(0); // force init_dvi_file
      QApplication::restoreOverrideCursor();
      foreGroundPaint.end();
      KMessageBox::error( this,
			  i18n("File corruption!\n\n") +
			  dvi_oops_msg +
			  i18n("\n\nMost likely this means that the DVI file\n") + 
			  filename +
			  i18n("\nis broken, or that it is not a DVI file."));
      return;
    } else {
      check_dvi_file();
      draw_page();
    }
    QApplication::restoreOverrideCursor();
    foreGroundPaint.end();
  }
  resize(pixmap->width(), pixmap->height());
  repaint();
}


bool dviWindow::correctDVI()
{
  QFile f(filename);
  if (!f.open(IO_ReadOnly))
    return FALSE;
  int n = f.size();
  if ( n < 134 )	// Too short for a dvi file
    return FALSE;
  f.at( n-4 );

  char test[4];
  unsigned char trailer[4] = { 0xdf,0xdf,0xdf,0xdf };

  if ( f.readBlock( test, 4 )<4 || strncmp( test, (char *) trailer, 4 ) )
    return FALSE;
  // We suppose now that the dvi file is complete	and OK
  return TRUE;
}


void dviWindow::changePageSize()
{
  if ( pixmap && pixmap->paintingActive() )
    return;

  int old_width = 0;
  if (pixmap) {
    old_width = pixmap->width();
    delete pixmap;
  }
  pixmap = new QPixmap( (int)page_w, (int)page_h );
  pixmap->fill( white );

  resize( page_w, page_h );
  currwin.win = mane.win = pixmap->handle();
  drawPage();
}

//------ setup the dvi interpreter (should do more here ?) ----------

void dviWindow::setFile( const char *fname )
{
  if (ChangesPossible){
    initDVI();
  }
  filename = fname;
  dvi_name = 0;
  drawPage();
}


//------ handling pages ----------


void dviWindow::gotoPage(int new_page)
{
  if (new_page<1)
    new_page = 1;
  if (new_page>total_pages)
    new_page = total_pages;
  if (new_page-1==current_page)
    return;
  current_page = new_page-1;
  drawPage();
}


int dviWindow::totalPages()
{
	return total_pages;
}


void dviWindow::setZoom(double zoom)
{
  double s    = basedpi/(xres*zoom);
  mane.shrinkfactor = currwin.shrinkfactor = s;
  _zoom             = zoom;

  page_w = (int)(unshrunk_page_w / mane.shrinkfactor  + 0.5) + 2;
  page_h = (int)(unshrunk_page_h / mane.shrinkfactor  + 0.5) + 2;

  reset_fonts();
  changePageSize();
}


void dviWindow::paintEvent(QPaintEvent *ev)
{
  if (pixmap) {
    QPainter p(this);
    p.drawPixmap(QPoint(0, 0), *pixmap);
  }
}

void dviWindow::mousePressEvent ( QMouseEvent * e )
{
#ifdef DEBUG_SPECIAL
  kdDebug() << "mouse event" << endl;
#endif
  for(int i=0; i<num_of_used_hyperlinks; i++) {
    if (hyperLinkList[i].box.contains(e->pos())) {
      if (hyperLinkList[i].linkText[0] == '#' ) {
#ifdef DEBUG_SPECIAL
	kdDebug() << "hit: local link to " << hyperLinkList[i].linkText << endl;
#endif
	QString locallink = hyperLinkList[i].linkText.mid(1); // Drop the '#' at the beginning
	for(int j=0; j<numAnchors; j++) {
	  if (locallink.compare(AnchorList_String[j]) == 0) {
	    // @@@Currently there is no-one listening. Make sure
	    // kviewshell has a slot for this.
	    emit(request_goto_page(AnchorList_Page[j], AnchorList_Vert[j]));
	    break;
	  }
	}
      } else {
#ifdef DEBUG_SPECIAL
	kdDebug() << "hit: external link to " << hyperLinkList[i].linkText << endl;
#endif
	QUrl DVI_Url(filename);
	QUrl Link_Url(DVI_Url, hyperLinkList[i].linkText, TRUE );

	KShellProcess proc;
	proc << "kfmclient openURL " << Link_Url.toString();
	proc.start(KProcess::Block);
	//@@@ Set up a warning requester if the command failed?
      }
      break;
    }
  }
}
