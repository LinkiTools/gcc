
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_peer_gtk_FreetypeGlyphVector__
#define __gnu_java_awt_peer_gtk_FreetypeGlyphVector__

#pragma interface

#include <java/awt/font/GlyphVector.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace java
    {
      namespace awt
      {
        namespace peer
        {
          namespace gtk
          {
              class FreetypeGlyphVector;
              class GdkFontPeer;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace awt
    {
        class Font;
        class Shape;
      namespace font
      {
          class FontRenderContext;
          class GlyphJustificationInfo;
          class GlyphMetrics;
          class GlyphVector;
      }
      namespace geom
      {
          class AffineTransform;
          class GeneralPath;
          class Point2D;
          class Rectangle2D;
      }
    }
  }
}

class gnu::java::awt::peer::gtk::FreetypeGlyphVector : public ::java::awt::font::GlyphVector
{

  void dispose(JArray< jlong > *);
  jlong getNativeFontPointer(jint);
public:
  FreetypeGlyphVector(::java::awt::Font *, ::java::lang::String *, ::java::awt::font::FontRenderContext *);
  FreetypeGlyphVector(::java::awt::Font *, JArray< jchar > *, jint, jint, ::java::awt::font::FontRenderContext *, jint);
  FreetypeGlyphVector(::java::awt::Font *, JArray< jint > *, ::java::awt::font::FontRenderContext *);
private:
  FreetypeGlyphVector(::gnu::java::awt::peer::gtk::FreetypeGlyphVector *);
public:
  virtual void finalize();
private:
  void getGlyphs();
public:
  virtual void getGlyphs(JArray< jint > *, JArray< jint > *, JArray< jlong > *);
private:
  ::java::awt::geom::Point2D * getKerning(jint, jint, jlong);
  JArray< jdouble > * getMetricsNative(jint, jlong);
  ::java::awt::geom::GeneralPath * getGlyphOutlineNative(jint, jlong);
public:
  virtual ::java::lang::Object * clone();
  virtual jboolean equals(::java::awt::font::GlyphVector *);
  virtual ::java::awt::Font * getFont();
  virtual ::java::awt::font::FontRenderContext * getFontRenderContext();
  virtual void performDefaultLayout();
  virtual jint getGlyphCode(jint);
  virtual JArray< jint > * getGlyphCodes(jint, jint, JArray< jint > *);
public: // actually protected
  virtual JArray< jlong > * getGlyphFonts(jint, jint, JArray< jlong > *);
public:
  virtual ::java::awt::Shape * getGlyphLogicalBounds(jint);
  virtual void setupGlyphMetrics();
  virtual ::java::awt::font::GlyphMetrics * getGlyphMetrics(jint);
  virtual ::java::awt::Shape * getGlyphOutline(jint);
  virtual ::java::awt::geom::Point2D * getGlyphPosition(jint);
  virtual JArray< jfloat > * getGlyphPositions(jint, jint, JArray< jfloat > *);
  virtual ::java::awt::geom::AffineTransform * getGlyphTransform(jint);
  virtual ::java::awt::Shape * getGlyphVisualBounds(jint);
  virtual ::java::awt::geom::Rectangle2D * getLogicalBounds();
  virtual jint getNumGlyphs();
  virtual ::java::awt::Shape * getOutline();
  virtual ::java::awt::font::GlyphJustificationInfo * getGlyphJustificationInfo(jint);
  virtual ::java::awt::Shape * getOutline(jfloat, jfloat);
  virtual ::java::awt::geom::Rectangle2D * getVisualBounds();
  virtual void setGlyphPosition(jint, ::java::awt::geom::Point2D *);
  virtual void setGlyphTransform(jint, ::java::awt::geom::AffineTransform *);
private:
  ::java::awt::Font * __attribute__((aligned(__alignof__( ::java::awt::font::GlyphVector)))) font;
  ::gnu::java::awt::peer::gtk::GdkFontPeer * peer;
  ::java::awt::geom::Rectangle2D * logicalBounds;
  JArray< jfloat > * glyphPositions;
  ::java::lang::String * s;
  ::java::awt::font::FontRenderContext * frc;
  jint nGlyphs;
  JArray< jint > * glyphCodes;
  JArray< jlong > * fontSet;
  JArray< ::java::awt::geom::AffineTransform * > * glyphTransforms;
  JArray< ::java::awt::font::GlyphMetrics * > * metricsCache;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_peer_gtk_FreetypeGlyphVector__
