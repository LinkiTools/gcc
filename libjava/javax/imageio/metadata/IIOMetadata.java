/* IIOMetadata.java --
   Copyright (C) 2004  Free Software Foundation, Inc.

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
Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301 USA.

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


package javax.imageio.metadata;

/**
 * @author Michael Koch (konqueror@gmx.de)
 */
public abstract class IIOMetadata
{
  protected IIOMetadataController controller;
  protected IIOMetadataController defaultController;
  protected String[] extraMetadataFormatClassNames;
  protected String[] extraMetadataFormatNames;
  protected String nativeMetadataFormatClassName;
  protected String nativeMetadataFormatName;
  protected boolean standardFormatSupported;

  /**
   * Creates a <code>IIOMetaData</code> object.
   */
  protected IIOMetadata()
  {
    // Do nothing here.
  }

  /**
   * Creates a <code>IIOMetaData</code> object with the given arguments.
   *
   * @param standardMetadataFormatSupported
   * @param nativeMetadataFormatName
   * @param nativeMetadataFormatClassName
   * @param extraMetadataFormatNames
   * @param extraMetadataFormatClassNames
   *
   * @throws IllegalArgumentException if extraMetadataFormatNames has length of
   * zero or extraMetadataFormatNames and extraMetadataFormatClassNames are
   * neither both null, not have the same length
   */
  protected IIOMetadata(boolean standardMetadataFormatSupported,
                        String nativeMetadataFormatName,
                        String nativeMetadataFormatClassName,
                        String[] extraMetadataFormatNames,
                        String[] extraMetadataFormatClassNames)
  {
    if (extraMetadataFormatNames != null
        && extraMetadataFormatNames.length == 0)
      throw new IllegalArgumentException
        ("extraMetadataFormatNames may not be empty");

    if (((extraMetadataFormatNames == null)
         && (extraMetadataFormatClassNames != null))
        || ((extraMetadataFormatNames != null)
            && (extraMetadataFormatClassNames == null))
        || ((extraMetadataFormatNames != null)
            && (extraMetadataFormatClassNames != null)
            && (extraMetadataFormatNames.length !=
                extraMetadataFormatClassNames.length)))
      throw new IllegalArgumentException
        ("extraMetadataFormatNames and extraMetadataFormatClassNames " +
         "have different lengths");

    this.standardFormatSupported = standardMetadataFormatSupported;
    this.nativeMetadataFormatName = nativeMetadataFormatName;
    this.nativeMetadataFormatClassName = nativeMetadataFormatClassName;
    this.extraMetadataFormatNames = extraMetadataFormatNames;
    this.extraMetadataFormatClassNames = extraMetadataFormatClassNames;
  }

  public boolean activateController()
  {
    if (! hasController())
      return false;

    return getDefaultController().activate(this);
  }

  public IIOMetadataController getController()
  {
    return controller;
  }

  public IIOMetadataController getDefaultController()
  {
    return defaultController;
  }

  public String[] getExtraMetadataFormatNames()
  {
    return (String[]) extraMetadataFormatNames.clone();
  }

  public IIOMetadataFormat getMetadataFormat(String formatName)
  {
    if (formatName == null)
      throw new IllegalArgumentException("formatName may not be null");
    
    String formatClassName = null;

    if (isStandardMetadataFormatSupported()
	&& formatName.equals(nativeMetadataFormatName))
      formatClassName = nativeMetadataFormatClassName;
    else
      {
	String[] extraFormatNames = getExtraMetadataFormatNames();
	
	for (int i = extraFormatNames.length - 1; i >= 0; --i)
	  if (extraFormatNames[i].equals(formatName))
	    {
	      formatClassName = extraFormatNames[i];
	      break;
	    }
      }

    if (formatClassName == null)
      throw new IllegalArgumentException("unknown format");

    IIOMetadataFormat format;
    
    try
      {
	format = (IIOMetadataFormat) Class.forName(formatClassName)
					  .newInstance();
      }
    catch (Exception e)
      {
	IllegalStateException ise = new IllegalStateException();
	ise.initCause(e);
	throw ise;
      }

    return format;
  }

  public String[] getMetadataFormatNames()
  {
    String[] formatNames = getExtraMetadataFormatNames();
    
    if (isStandardMetadataFormatSupported())
      {
        // Combine native metadata format name and extra metadata format names
	// into one String array.
	String[] tmp = new String[formatNames.length + 1];
	tmp[0] = getNativeMetadataFormatName();

	for (int i = 1; i < tmp.length; ++i)
	  tmp[i] = formatNames[i - 1];

	formatNames = tmp;
      }

    return formatNames;
  }

  public String getNativeMetadataFormatName()
  {
    return nativeMetadataFormatName;
  }

  public boolean hasController()
  {
    return getController() != null;
  }

  public abstract boolean isReadOnly();

  public boolean isStandardMetadataFormatSupported()
  {
    return standardFormatSupported;
  }

  public abstract void reset();

  public void setController(IIOMetadataController controller)
  {
    this.controller = controller;
  }
}
