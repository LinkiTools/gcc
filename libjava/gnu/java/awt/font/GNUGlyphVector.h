
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_font_GNUGlyphVector__
#define __gnu_java_awt_font_GNUGlyphVector__

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
        namespace font
        {
            class FontDelegate;
            class GNUGlyphVector;
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
          class Point2D;
          class Rectangle2D;
      }
    }
  }
}

class gnu::java::awt::font::GNUGlyphVector : public ::java::awt::font::GlyphVector
{

public:
  GNUGlyphVector(::gnu::java::awt::font::FontDelegate *, ::java::awt::Font *, ::java::awt::font::FontRenderContext *, JArray< jint > *);
  virtual ::java::awt::Font * getFont();
  virtual ::java::awt::font::FontRenderContext * getFontRenderContext();
  virtual void performDefaultLayout();
  virtual jint getNumGlyphs();
  virtual jint getGlyphCode(jint);
  virtual JArray< jint > * getGlyphCodes(jint, jint, JArray< jint > *);
  virtual ::java::awt::geom::Rectangle2D * getLogicalBounds();
  virtual ::java::awt::geom::Rectangle2D * getVisualBounds();
  virtual ::java::awt::Shape * getOutline();
  virtual ::java::awt::Shape * getOutline(jfloat, jfloat);
  virtual ::java::awt::Shape * getOutline(jfloat, jfloat, jint);
  virtual ::java::awt::Shape * getGlyphOutline(jint);
  virtual ::java::awt::Shape * getGlyphOutline(jint, jint);
  virtual ::java::awt::geom::Point2D * getGlyphPosition(jint);
  virtual void setGlyphPosition(jint, ::java::awt::geom::Point2D *);
  virtual ::java::awt::geom::AffineTransform * getGlyphTransform(jint);
  virtual void setGlyphTransform(jint, ::java::awt::geom::AffineTransform *);
  virtual jint getLayoutFlags();
  virtual JArray< jfloat > * getGlyphPositions(jint, jint, JArray< jfloat > *);
private:
  jfloat getAscent();
  jfloat getDescent();
public:
  virtual ::java::awt::Shape * getGlyphLogicalBounds(jint);
  virtual ::java::awt::Shape * getGlyphVisualBounds(jint);
  virtual ::java::awt::font::GlyphMetrics * getGlyphMetrics(jint);
  virtual ::java::awt::font::GlyphJustificationInfo * getGlyphJustificationInfo(jint);
  virtual jboolean equals(::java::awt::font::GlyphVector *);
private:
  void validate();
  ::gnu::java::awt::font::FontDelegate * __attribute__((aligned(__alignof__( ::java::awt::font::GlyphVector)))) fontDelegate;
  ::java::awt::Font * font;
  ::java::awt::font::FontRenderContext * renderContext;
  JArray< jint > * glyphs;
  jfloat fontSize;
  ::java::awt::geom::AffineTransform * transform;
  jboolean valid;
  JArray< jfloat > * pos;
  JArray< ::java::awt::geom::AffineTransform * > * transforms;
  jint layoutFlags;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_font_GNUGlyphVector__
