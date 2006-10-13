
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_awt_image_SinglePixelPackedSampleModel__
#define __java_awt_image_SinglePixelPackedSampleModel__

#pragma interface

#include <java/awt/image/SampleModel.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace awt
    {
      namespace image
      {
          class DataBuffer;
          class SampleModel;
          class SinglePixelPackedSampleModel;
      }
    }
  }
}

class java::awt::image::SinglePixelPackedSampleModel : public ::java::awt::image::SampleModel
{

public:
  SinglePixelPackedSampleModel(jint, jint, jint, JArray< jint > *);
  SinglePixelPackedSampleModel(jint, jint, jint, jint, JArray< jint > *);
  virtual jint getNumDataElements();
  virtual ::java::awt::image::SampleModel * createCompatibleSampleModel(jint, jint);
  virtual ::java::awt::image::DataBuffer * createDataBuffer();
  virtual JArray< jint > * getSampleSize();
  virtual jint getSampleSize(jint);
  virtual jint getOffset(jint, jint);
  virtual JArray< jint > * getBitOffsets();
  virtual JArray< jint > * getBitMasks();
  virtual jint getScanlineStride();
  virtual ::java::awt::image::SampleModel * createSubsetSampleModel(JArray< jint > *);
  virtual ::java::lang::Object * getDataElements(jint, jint, ::java::lang::Object *, ::java::awt::image::DataBuffer *);
  virtual ::java::lang::Object * getDataElements(jint, jint, jint, jint, ::java::lang::Object *, ::java::awt::image::DataBuffer *);
  virtual JArray< jint > * getPixel(jint, jint, JArray< jint > *, ::java::awt::image::DataBuffer *);
  virtual JArray< jint > * getPixels(jint, jint, jint, jint, JArray< jint > *, ::java::awt::image::DataBuffer *);
  virtual jint getSample(jint, jint, jint, ::java::awt::image::DataBuffer *);
  virtual void setDataElements(jint, jint, jint, jint, ::java::lang::Object *, ::java::awt::image::DataBuffer *);
  virtual void setDataElements(jint, jint, ::java::lang::Object *, ::java::awt::image::DataBuffer *);
  virtual void setPixel(jint, jint, JArray< jint > *, ::java::awt::image::DataBuffer *);
  virtual void setPixels(jint, jint, jint, jint, JArray< jint > *, ::java::awt::image::DataBuffer *);
  virtual void setSample(jint, jint, jint, jint, ::java::awt::image::DataBuffer *);
  virtual jboolean equals(::java::lang::Object *);
  virtual jint hashCode();
  virtual ::java::lang::String * toString();
private:
  jint __attribute__((aligned(__alignof__( ::java::awt::image::SampleModel)))) scanlineStride;
  JArray< jint > * bitMasks;
  JArray< jint > * bitOffsets;
  JArray< jint > * sampleSize;
public:
  static ::java::lang::Class class$;
};

#endif // __java_awt_image_SinglePixelPackedSampleModel__
