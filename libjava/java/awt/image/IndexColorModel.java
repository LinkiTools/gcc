/* IndexColorModel.java -- Java class for interpreting Pixel objects
   Copyright (C) 1999 Free Software Foundation, Inc.

This file is part of GNU Classpath.

GNU Classpath is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Classpath is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Classpath; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
02111-1307 USA.

Linking this library statically or dynamically with other modules is
making a combined work based on this library.  Thus, the terms and
conditions of the GNU General Public License cover the whole
combination.

As a special exception, the copyright holders of this library give you
permission to link this library with independent modules to produce an
executable, regardless of the license terms of these independent
modules, and to copy and distribute the resulting executable under
terms of your choice, provided that you also meet, for each linked
independent module, the terms and conditions of the license of that
module.  An independent module is a module which is not derived from
or based on this library.  If you modify this library, you may extend
this exception to your version of the library, but you are not
obligated to do so.  If you do not wish to do so, delete this
exception statement from your version. */


package java.awt.image;

import java.awt.color.ColorSpace;
import java.math.BigInteger;

/**
 * Color model similar to pseudo visual in X11.
 *
 * This color model maps linear pixel values to actual RGB and alpha colors.
 * Thus, pixel values are indexes into the color map.  Each color component is
 * an 8-bit unsigned value.
 *
 * The IndexColorModel supports a map of valid pixels, allowing the
 * representation of holes in the the color map.  The valid map is represented
 * as a BigInteger where each bit indicates the validity of the map entry with
 * the same index.
 * 
 * Colors can have alpha components for transparency support.  If alpha
 * component values aren't given, color values are opaque.  The model also
 * supports a reserved pixel value to represent completely transparent colors,
 * no matter what the actual color component values are.
 *
 * IndexColorModel supports anywhere from 1 to 16 bit index values.  The
 * allowed transfer types are DataBuffer.TYPE_BYTE and DataBuffer.TYPE_USHORT.
 *
 * @author C. Brian Jones (cbj@gnu.org) 
 */
public class IndexColorModel extends ColorModel
{
  private int map_size;
  private boolean opaque;
  private int trans = -1;
  private int[] rgb;
  private BigInteger validBits = new BigInteger("0");

  /**
   * Each array much contain <code>size</code> elements.  For each 
   * array, the i-th color is described by reds[i], greens[i], 
   * blues[i], alphas[i], unless alphas is not specified, then all the 
   * colors are opaque except for the transparent color. 
   *
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param reds the red component of all colors
   * @param greens the green component of all colors
   * @param blues the blue component of all colors
   */
  public IndexColorModel(int bits, int size, byte[] reds, byte[] greens,
                         byte[] blues)
  {
    this (bits, size, reds, greens, blues, (byte[]) null);
  }

  /**
   * Each array much contain <code>size</code> elements.  For each 
   * array, the i-th color is described by reds[i], greens[i], 
   * blues[i], alphas[i], unless alphas is not specified, then all the 
   * colors are opaque except for the transparent color. 
   *
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param reds the red component of all colors
   * @param greens the green component of all colors
   * @param blues the blue component of all colors
   * @param trans the index of the transparent color
   */
  public IndexColorModel(int bits, int size, byte[] reds, byte[] greens,
                         byte[] blues, int trans)
  {
    this (bits, size, reds, greens, blues, (byte[]) null);
    this.trans = trans;
  }

  /**
   * Each array much contain <code>size</code> elements.  For each 
   * array, the i-th color is described by reds[i], greens[i], 
   * blues[i], alphas[i], unless alphas is not specified, then all the 
   * colors are opaque except for the transparent color. 
   *
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param reds the red component of all colors
   * @param greens the green component of all colors
   * @param blues the blue component of all colors
   * @param alphas the alpha component of all colors
   */
  public IndexColorModel(int bits, int size, byte[] reds, byte[] greens,
                         byte[] blues, byte[] alphas)
  {
    super (bits);
    map_size = size;
    opaque = (alphas == null);

    rgb = new int[size];
    if (alphas == null)
      {
        for (int i = 0; i < size; i++)
          {
            rgb[i] = (0xff000000
                      | ((reds[i] & 0xff) << 16)
                      | ((greens[i] & 0xff) << 8)
                      | (blues[i] & 0xff));
          }
      }
    else
      {
        for (int i = 0; i < size; i++)
          {
            rgb[i] = ((alphas[i] & 0xff) << 24
                      | ((reds[i] & 0xff) << 16)
                      | ((greens[i] & 0xff) << 8)
                      | (blues[i] & 0xff));
          }
      }

    // Generate a bigint with 1's for every pixel
    validBits.setBit(size);
    validBits.subtract(new BigInteger("1"));
  }

  /**
   * Each array much contain <code>size</code> elements.  For each 
   * array, the i-th color is described by reds[i], greens[i], 
   * blues[i], alphas[i], unless alphas is not specified, then all the 
   * colors are opaque except for the transparent color. 
   *
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param cmap packed color components
   * @param start the offset of the first color component in <code>cmap</code>
   * @param hasAlpha <code>cmap</code> has alpha values
   */
  public IndexColorModel (int bits, int size, byte[] cmap, int start, 
                          boolean hasAlpha)
  {
    this (bits, size, cmap, start, hasAlpha, -1);
  }

  /**
   * Each array much contain <code>size</code> elements.  For each 
   * array, the i-th color is described by reds[i], greens[i], 
   * blues[i], alphas[i], unless alphas is not specified, then all the 
   * colors are opaque except for the transparent color. 
   *
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param cmap packed color components
   * @param start the offset of the first color component in <code>cmap</code>
   * @param hasAlpha <code>cmap</code> has alpha values
   * @param trans the index of the transparent color
   */
  public IndexColorModel (int bits, int size, byte[] cmap, int start, 
                          boolean hasAlpha, int trans)
  {
    super (bits);
    map_size = size;
    opaque = !hasAlpha;
    this.trans = trans;
    // Generate a bigint with 1's for every pixel
    validBits.setBit(size);
    validBits.subtract(new BigInteger("1"));
  }

  /**
   * Each array much contain <code>size</code> elements.  For each 
   * array, the i-th color is described by reds[i], greens[i], 
   * blues[i], alphas[i], unless alphas is not specified, then all the 
   * colors are opaque except for the transparent color. 
   *
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param cmap packed color components
   * @param start the offset of the first color component in <code>cmap</code>
   * @param hasAlpha <code>cmap</code> has alpha values
   * @param trans the index of the transparent color
   * @param transferType DataBuffer.TYPE_BYTE or DataBuffer.TYPE_USHORT
   */
  public IndexColorModel (int bits, int size, byte[] cmap, int start, 
                          boolean hasAlpha, int trans, int transferType)
  {
    super(bits * 4, // total bits, sRGB, four channels
	  nArray(bits, 4), // bits for each channel
	  ColorSpace.getInstance(ColorSpace.CS_sRGB), // sRGB
	  true, // has alpha
	  false, // not premultiplied
	  TRANSLUCENT, transferType);
    if (transferType != DataBuffer.TYPE_BYTE
        && transferType != DataBuffer.TYPE_USHORT)
      throw new IllegalArgumentException();
    map_size = size;
    opaque = !hasAlpha;
    this.trans = trans;
    // Generate a bigint with 1's for every pixel
    validBits.setBit(size);
    validBits.subtract(new BigInteger("1"));
  }

  /**
   * Construct an IndexColorModel using a colormap with holes.
   * 
   * The IndexColorModel is built from the array of ints defining the
   * colormap.  Each element contains red, green, blue, and alpha
   * components.    The ColorSpace is sRGB.  The transparency value is
   * automatically determined.
   * 
   * This constructor permits indicating which colormap entries are valid,
   * using the validBits argument.  Each entry in cmap is valid if the
   * corresponding bit in validBits is set.  
   * 
   * @param bits the number of bits needed to represent <code>size</code> colors
   * @param size the number of colors in the color map
   * @param cmap packed color components
   * @param start the offset of the first color component in <code>cmap</code>
   * @param transferType DataBuffer.TYPE_BYTE or DataBuffer.TYPE_USHORT
   */
  public IndexColorModel (int bits, int size, int[] cmap, int start, 
                          int transferType, BigInteger validBits)
  {
    super(bits * 4, // total bits, sRGB, four channels
	  nArray(bits, 4), // bits for each channel
	  ColorSpace.getInstance(ColorSpace.CS_sRGB), // sRGB
	  true, // has alpha
	  false, // not premultiplied
	  TRANSLUCENT, transferType);
    if (transferType != DataBuffer.TYPE_BYTE
        && transferType != DataBuffer.TYPE_USHORT)
      throw new IllegalArgumentException();
    map_size = size;
    opaque = false;
    this.trans = -1;
    this.validBits = validBits;
  }

  public final int getMapSize ()
  {
    return map_size;
  }

  /**
   * Get the index of the transparent color in this color model
   */
  public final int getTransparentPixel ()
  {
    return trans;
  }

  /**
   * <br>
   */
  public final void getReds (byte[] r)
  {
    getComponents (r, 2);
  }

  /**
   * <br>
   */
  public final void getGreens (byte[] g)
  {
    getComponents (g, 1);
  }

  /**
   * <br>
   */
  public final void getBlues (byte[] b)
  {
    getComponents (b, 0);
  }

  /**
   * <br>
   */
  public final void getAlphas (byte[] a)
  {
    getComponents (a, 3);
  }

  private void getComponents (byte[] c, int ci)
  {
    int i, max = (map_size < c.length) ? map_size : c.length;
    for (i = 0; i < max; i++)
	    c[i] = (byte) ((generateMask (ci)  & rgb[i]) >> (ci * pixel_bits));
  } 

  /**
   * Get the red component of the given pixel.
   */
  public final int getRed (int pixel)
  {
    if (pixel < map_size)
	    return (int) ((generateMask (2) & rgb[pixel]) >> (2 * pixel_bits));
    
    return 0;
  }

  /**
   * Get the green component of the given pixel.
   */
  public final int getGreen (int pixel)
  {
    if (pixel < map_size)
	    return (int) ((generateMask (1) & rgb[pixel]) >> (1 * pixel_bits));
    
    return 0;
  }

  /**
   * Get the blue component of the given pixel.
   */
  public final int getBlue (int pixel)
  {
    if (pixel < map_size) 
	    return (int) (generateMask (0) & rgb[pixel]);
    
    return 0;
  }

  /**
   * Get the alpha component of the given pixel.
   */
  public final int getAlpha (int pixel)
  {
    if (pixel < map_size)
	    return (int) ((generateMask (3) & rgb[pixel]) >> (3 * pixel_bits));
    
    return 0;
  }

  /**
   * Get the RGB color value of the given pixel using the default
   * RGB color model. 
   *
   * @param pixel a pixel value
   */
  public final int getRGB (int pixel)
  {
    if (pixel < map_size)
	    return rgb[pixel];
    
    return 0;
  }
    
  //pixel_bits is number of bits to be in generated mask
  private int generateMask (int offset)
  {
    return (((2 << pixel_bits ) - 1) << (pixel_bits * offset));
  }

  /** Return true if pixel is valid, false otherwise. */
  public boolean isValid(int pixel)
  {
    return validBits.testBit(pixel);
  }
  
  /** Return true if all pixels are valid, false otherwise. */
  public boolean isValid()
  {
    // Generate a bigint with 1's for every pixel
    BigInteger allbits = new BigInteger("0");
    allbits.setBit(map_size);
    allbits.subtract(new BigInteger("1"));
    return allbits.equals(validBits);
  }
  
  /** 
   * Returns a BigInteger where each bit represents an entry in the color
   * model.  If the bit is on, the entry is valid.
   */
  public BigInteger getValidPixels()
  {
    return validBits;
  }
}

