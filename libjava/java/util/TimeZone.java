/* java.util.TimeZone
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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


package java.util;
import gnu.classpath.Configuration;

import java.io.*;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.text.DateFormatSymbols;

/**
 * This class represents a time zone offset and handles daylight savings.
 * 
 * You can get the default time zone with <code>getDefault</code>.
 * This represents the time zone where program is running.
 *
 * Another way to create a time zone is <code>getTimeZone</code>, where
 * you can give an identifier as parameter.  For instance, the identifier
 * of the Central European Time zone is "CET".
 *
 * With the <code>getAvailableIDs</code> method, you can get all the
 * supported time zone identifiers.
 *
 * @see Calendar
 * @see SimpleTimeZone
 * @author Jochen Hoenicke
 */
public abstract class TimeZone implements java.io.Serializable, Cloneable
{

  /**
   * Constant used to indicate that a short timezone abbreviation should
   * be returned, such as "EST"
   */
  public static final int SHORT = 0;

  /**
   * Constant used to indicate that a long timezone name should be
   * returned, such as "Eastern Standard Time".
   */
  public static final int LONG = 1;

  /**
   * The time zone identifier, e.g. PST.
   */
  private String ID;

  /**
   * The default time zone, as returned by getDefault.
   */
  private static TimeZone defaultZone0;

  /**
   * Tries to get the default TimeZone for this system if not already
   * set.  It will call <code>getDefaultTimeZone(String)</code> with
   * the result of
   * <code>System.getProperty("user.timezone")</code>,
   * <code>System.getenv("TZ")</code>,
   * <code>readTimeZoneFile("/etc/timezone")</code>,
   * <code>readtzFile("/etc/localtime")</code> and
   * <code>getDefaultTimeZoneId()</code>
   * till a supported TimeZone is found.
   * If every method fails GMT is returned.
   */
  private static synchronized TimeZone defaultZone()
  {
    /* Look up default timezone */
    if (defaultZone0 == null) 
      {
	defaultZone0 = (TimeZone) AccessController.doPrivileged
	  (new PrivilegedAction()
	    {
	      public Object run()
	      {
		if (Configuration.INIT_LOAD_LIBRARY)
		  {
		    System.loadLibrary("javautil");
		  }
		
		TimeZone zone = null;
		
		// Prefer System property user.timezone.
		String tzid = System.getProperty("user.timezone");
		if (tzid != null && !tzid.equals(""))
		  zone = getDefaultTimeZone(tzid);
		
		// See if TZ environment variable is set and accessible.
		if (zone == null)
		  {
		    tzid = System.getenv("TZ");
		    if (tzid != null && !tzid.equals(""))
		      zone = getDefaultTimeZone(tzid);
		  }
		
		// Try to parse /etc/timezone.
		if (zone == null)
		  {
		    tzid = readTimeZoneFile("/etc/timezone");
		    if (tzid != null && !tzid.equals(""))
		      zone = getDefaultTimeZone(tzid);
		  }
		
		// Try to parse /etc/localtime
		if (zone == null)
		  {
		    tzid = readtzFile("/etc/localtime");
		    if (tzid != null && !tzid.equals(""))
		      zone = getDefaultTimeZone(tzid);
		  }
		
		// Try some system specific way
		if (zone == null)
		  {
		    tzid = getDefaultTimeZoneId();
		    if (tzid != null && !tzid.equals(""))
		      zone = getDefaultTimeZone(tzid);
		  }
		
		// Fall back on GMT.
		if (zone == null)
		  zone = (TimeZone) timezones().get("GMT");
		
		return zone;
	      }
	    });
      }
    
    return defaultZone0; 
  }
  
  private static final long serialVersionUID = 3581463369166924961L;

  /**
   * HashMap for timezones by ID.  
   */
  private static HashMap timezones0;
  /* initialize this static field lazily to overhead if
   * it is not needed: 
   */
  private static synchronized HashMap timezones()
  {
    if (timezones0 == null) 
      {
	HashMap timezones = new HashMap();
	timezones0 = timezones;

	TimeZone tz;
	// Automatically generated by scripts/timezones.pl
	// XXX - Should we read this data from a file?
	tz = new SimpleTimeZone(-11000 * 3600, "MIT");
	timezones0.put("MIT", tz);
	timezones0.put("Pacific/Apia", tz);
	timezones0.put("Pacific/Midway", tz);
	timezones0.put("Pacific/Niue", tz);
	timezones0.put("Pacific/Pago_Pago", tz);
	tz = new SimpleTimeZone
	  (-10000 * 3600, "America/Adak",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("America/Adak", tz);
	tz = new SimpleTimeZone(-10000 * 3600, "HST");
	timezones0.put("HST", tz);
	timezones0.put("Pacific/Fakaofo", tz);
	timezones0.put("Pacific/Honolulu", tz);
	timezones0.put("Pacific/Johnston", tz);
	timezones0.put("Pacific/Rarotonga", tz);
	timezones0.put("Pacific/Tahiti", tz);
	tz = new SimpleTimeZone(-9500 * 3600, "Pacific/Marquesas");
	timezones0.put("Pacific/Marquesas", tz);
	tz = new SimpleTimeZone
	  (-9000 * 3600, "AST",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("AST", tz);
	timezones0.put("America/Anchorage", tz);
	timezones0.put("America/Juneau", tz);
	timezones0.put("America/Nome", tz);
	timezones0.put("America/Yakutat", tz);
	tz = new SimpleTimeZone(-9000 * 3600, "Pacific/Gambier");
	timezones0.put("Pacific/Gambier", tz);
	tz = new SimpleTimeZone
	  (-8000 * 3600, "PST",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("PST", tz);
	timezones0.put("PST8PDT", tz);
	timezones0.put("America/Dawson", tz);
	timezones0.put("America/Los_Angeles", tz);
	timezones0.put("America/Tijuana", tz);
	timezones0.put("America/Vancouver", tz);
	timezones0.put("America/Whitehorse", tz);
	timezones0.put("US/Pacific-New", tz);
	tz = new SimpleTimeZone(-8000 * 3600, "Pacific/Pitcairn");
	timezones0.put("Pacific/Pitcairn", tz);
	tz = new SimpleTimeZone
	  (-7000 * 3600, "MST",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("MST", tz);
	timezones0.put("MST7MDT", tz);
	timezones0.put("America/Boise", tz);
	timezones0.put("America/Chihuahua", tz);
	timezones0.put("America/Denver", tz);
	timezones0.put("America/Edmonton", tz);
	timezones0.put("America/Inuvik", tz);
	timezones0.put("America/Mazatlan", tz);
	timezones0.put("America/Shiprock", tz);
	timezones0.put("America/Yellowknife", tz);
	tz = new SimpleTimeZone(-7000 * 3600, "MST7");
	timezones0.put("MST7", tz);
	timezones0.put("PNT", tz);
	timezones0.put("America/Dawson_Creek", tz);
	timezones0.put("America/Hermosillo", tz);
	timezones0.put("America/Phoenix", tz);
	tz = new SimpleTimeZone
	  (-6000 * 3600, "CST",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("CST", tz);
	timezones0.put("CST6CDT", tz);
	timezones0.put("America/Cambridge_Bay", tz);
	timezones0.put("America/Cancun", tz);
	timezones0.put("America/Chicago", tz);
	timezones0.put("America/Menominee", tz);
	timezones0.put("America/Merida", tz);
	timezones0.put("America/Mexico_City", tz);
	timezones0.put("America/Monterrey", tz);
	timezones0.put("America/Rainy_River", tz);
	timezones0.put("America/Winnipeg", tz);
	tz = new SimpleTimeZone(-6000 * 3600, "America/Belize");
	timezones0.put("America/Belize", tz);
	timezones0.put("America/Costa_Rica", tz);
	timezones0.put("America/El_Salvador", tz);
	timezones0.put("America/Guatemala", tz);
	timezones0.put("America/Managua", tz);
	timezones0.put("America/Regina", tz);
	timezones0.put("America/Swift_Current", tz);
	timezones0.put("America/Tegucigalpa", tz);
	timezones0.put("Pacific/Galapagos", tz);
	tz = new SimpleTimeZone
	  (-6000 * 3600, "Pacific/Easter",
	   Calendar.OCTOBER, 9, -Calendar.SUNDAY, 0 * 3600,
	   Calendar.MARCH, 9, -Calendar.SUNDAY, 0 * 3600);
	timezones0.put("Pacific/Easter", tz);
	tz = new SimpleTimeZone
	  (-5000 * 3600, "America/Grand_Turk",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("America/Grand_Turk", tz);
	timezones0.put("America/Havana", tz);
	tz = new SimpleTimeZone(-5000 * 3600, "EST5");
	timezones0.put("EST5", tz);
	timezones0.put("IET", tz);
	timezones0.put("America/Bogota", tz);
	timezones0.put("America/Cayman", tz);
	timezones0.put("America/Eirunepe", tz);
	timezones0.put("America/Guayaquil", tz);
	timezones0.put("America/Indiana/Indianapolis", tz);
	timezones0.put("America/Indiana/Knox", tz);
	timezones0.put("America/Indiana/Marengo", tz);
	timezones0.put("America/Indiana/Vevay", tz);
	timezones0.put("America/Indianapolis", tz);
	timezones0.put("America/Iqaluit", tz);
	timezones0.put("America/Jamaica", tz);
	timezones0.put("America/Lima", tz);
	timezones0.put("America/Panama", tz);
	timezones0.put("America/Pangnirtung", tz);
	timezones0.put("America/Port-au-Prince", tz);
	timezones0.put("America/Porto_Acre", tz);
	timezones0.put("America/Rankin_Inlet", tz);
	tz = new SimpleTimeZone
	  (-5000 * 3600, "EST",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("EST", tz);
	timezones0.put("EST5EDT", tz);
	timezones0.put("America/Detroit", tz);
	timezones0.put("America/Kentucky/Louisville", tz);
	timezones0.put("America/Kentucky/Monticello", tz);
	timezones0.put("America/Louisville", tz);
	timezones0.put("America/Montreal", tz);
	timezones0.put("America/Nassau", tz);
	timezones0.put("America/New_York", tz);
	timezones0.put("America/Nipigon", tz);
	timezones0.put("America/Thunder_Bay", tz);
	tz = new SimpleTimeZone(-4000 * 3600, "PRT");
	timezones0.put("PRT", tz);
	timezones0.put("America/Anguilla", tz);
	timezones0.put("America/Antigua", tz);
	timezones0.put("America/Aruba", tz);
	timezones0.put("America/Barbados", tz);
	timezones0.put("America/Boa_Vista", tz);
	timezones0.put("America/Caracas", tz);
	timezones0.put("America/Curacao", tz);
	timezones0.put("America/Dominica", tz);
	timezones0.put("America/Grenada", tz);
	timezones0.put("America/Guadeloupe", tz);
	timezones0.put("America/Guyana", tz);
	timezones0.put("America/La_Paz", tz);
	timezones0.put("America/Manaus", tz);
	timezones0.put("America/Martinique", tz);
	timezones0.put("America/Montserrat", tz);
	timezones0.put("America/Port_of_Spain", tz);
	timezones0.put("America/Porto_Velho", tz);
	timezones0.put("America/Puerto_Rico", tz);
	timezones0.put("America/Santo_Domingo", tz);
	timezones0.put("America/St_Kitts", tz);
	timezones0.put("America/St_Lucia", tz);
	timezones0.put("America/St_Thomas", tz);
	timezones0.put("America/St_Vincent", tz);
	timezones0.put("America/Tortola", tz);
	tz = new SimpleTimeZone
	  (-4000 * 3600, "America/Asuncion",
	   Calendar.OCTOBER, 1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.FEBRUARY, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("America/Asuncion", tz);
	tz = new SimpleTimeZone
	  (-4000 * 3600, "America/Cuiaba",
	   Calendar.OCTOBER, 2, Calendar.SUNDAY, 0 * 3600,
	   Calendar.FEBRUARY, 3, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("America/Cuiaba", tz);
	tz = new SimpleTimeZone
	  (-4000 * 3600, "America/Goose_Bay",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 60000,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 60000);
	timezones0.put("America/Goose_Bay", tz);
	tz = new SimpleTimeZone
	  (-4000 * 3600, "America/Glace_Bay",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("America/Glace_Bay", tz);
	timezones0.put("America/Halifax", tz);
	timezones0.put("America/Thule", tz);
	timezones0.put("Atlantic/Bermuda", tz);
	tz = new SimpleTimeZone
	  (-4000 * 3600, "America/Santiago",
	   Calendar.OCTOBER, 9, -Calendar.SUNDAY, 0 * 3600,
	   Calendar.MARCH, 9, -Calendar.SUNDAY, 0 * 3600);
	timezones0.put("America/Santiago", tz);
	timezones0.put("Antarctica/Palmer", tz);
	tz = new SimpleTimeZone
	  (-4000 * 3600, "Atlantic/Stanley",
	   Calendar.SEPTEMBER, 2, Calendar.SUNDAY, 0 * 3600,
	   Calendar.APRIL, 16, -Calendar.SUNDAY, 0 * 3600);
	timezones0.put("Atlantic/Stanley", tz);
	tz = new SimpleTimeZone
	  (-3500 * 3600, "CNT",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 60000,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 60000);
	timezones0.put("CNT", tz);
	timezones0.put("America/St_Johns", tz);
	tz = new SimpleTimeZone
	  (-3000 * 3600, "America/Araguaina",
	   Calendar.OCTOBER, 2, Calendar.SUNDAY, 0 * 3600,
	   Calendar.FEBRUARY, 3, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("America/Araguaina", tz);
	timezones0.put("America/Sao_Paulo", tz);
	tz = new SimpleTimeZone(-3000 * 3600, "AGT");
	timezones0.put("AGT", tz);
	timezones0.put("America/Belem", tz);
	timezones0.put("America/Buenos_Aires", tz);
	timezones0.put("America/Catamarca", tz);
	timezones0.put("America/Cayenne", tz);
	timezones0.put("America/Cordoba", tz);
	timezones0.put("America/Fortaleza", tz);
	timezones0.put("America/Jujuy", tz);
	timezones0.put("America/Maceio", tz);
	timezones0.put("America/Mendoza", tz);
	timezones0.put("America/Montevideo", tz);
	timezones0.put("America/Paramaribo", tz);
	timezones0.put("America/Recife", tz);
	timezones0.put("America/Rosario", tz);
	tz = new SimpleTimeZone
	  (-3000 * 3600, "America/Godthab",
	   Calendar.MARCH, 30, -Calendar.SATURDAY, 22000 * 3600,
	   Calendar.OCTOBER, 30, -Calendar.SATURDAY, 22000 * 3600);
	timezones0.put("America/Godthab", tz);
	tz = new SimpleTimeZone
	  (-3000 * 3600, "America/Miquelon",
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("America/Miquelon", tz);
	tz = new SimpleTimeZone(-2000 * 3600, "America/Noronha");
	timezones0.put("America/Noronha", tz);
	timezones0.put("Atlantic/South_Georgia", tz);
	tz = new SimpleTimeZone
	  (-1000 * 3600, "America/Scoresbysund",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("America/Scoresbysund", tz);
	timezones0.put("Atlantic/Azores", tz);
	tz = new SimpleTimeZone(-1000 * 3600, "Atlantic/Cape_Verde");
	timezones0.put("Atlantic/Cape_Verde", tz);
	timezones0.put("Atlantic/Jan_Mayen", tz);
	tz = new SimpleTimeZone(0 * 3600, "GMT");
	timezones0.put("GMT", tz);
	timezones0.put("UTC", tz);
	timezones0.put("Africa/Abidjan", tz);
	timezones0.put("Africa/Accra", tz);
	timezones0.put("Africa/Bamako", tz);
	timezones0.put("Africa/Banjul", tz);
	timezones0.put("Africa/Bissau", tz);
	timezones0.put("Africa/Casablanca", tz);
	timezones0.put("Africa/Conakry", tz);
	timezones0.put("Africa/Dakar", tz);
	timezones0.put("Africa/El_Aaiun", tz);
	timezones0.put("Africa/Freetown", tz);
	timezones0.put("Africa/Lome", tz);
	timezones0.put("Africa/Monrovia", tz);
	timezones0.put("Africa/Nouakchott", tz);
	timezones0.put("Africa/Ouagadougou", tz);
	timezones0.put("Africa/Sao_Tome", tz);
	timezones0.put("Africa/Timbuktu", tz);
	timezones0.put("Atlantic/Reykjavik", tz);
	timezones0.put("Atlantic/St_Helena", tz);
	timezones0.put("Europe/Belfast", tz);
	timezones0.put("Europe/Dublin", tz);
	timezones0.put("Europe/London", tz);
	tz = new SimpleTimeZone
	  (0 * 3600, "WET",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 1000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 1000 * 3600);
	timezones0.put("WET", tz);
	timezones0.put("Atlantic/Canary", tz);
	timezones0.put("Atlantic/Faeroe", tz);
	timezones0.put("Atlantic/Madeira", tz);
	timezones0.put("Europe/Lisbon", tz);
	tz = new SimpleTimeZone(1000 * 3600, "Africa/Algiers");
	timezones0.put("Africa/Algiers", tz);
	timezones0.put("Africa/Bangui", tz);
	timezones0.put("Africa/Brazzaville", tz);
	timezones0.put("Africa/Douala", tz);
	timezones0.put("Africa/Kinshasa", tz);
	timezones0.put("Africa/Lagos", tz);
	timezones0.put("Africa/Libreville", tz);
	timezones0.put("Africa/Luanda", tz);
	timezones0.put("Africa/Malabo", tz);
	timezones0.put("Africa/Ndjamena", tz);
	timezones0.put("Africa/Niamey", tz);
	timezones0.put("Africa/Porto-Novo", tz);
	timezones0.put("Africa/Tunis", tz);
	tz = new SimpleTimeZone
	  (1000 * 3600, "Africa/Windhoek",
	   Calendar.SEPTEMBER, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.APRIL, 1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Africa/Windhoek", tz);
	tz = new SimpleTimeZone
	  (1000 * 3600, "CET",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("CET", tz);
	timezones0.put("ECT", tz);
	timezones0.put("MET", tz);
	timezones0.put("Africa/Ceuta", tz);
	timezones0.put("Arctic/Longyearbyen", tz);
	timezones0.put("Europe/Amsterdam", tz);
	timezones0.put("Europe/Andorra", tz);
	timezones0.put("Europe/Belgrade", tz);
	timezones0.put("Europe/Berlin", tz);
	timezones0.put("Europe/Bratislava", tz);
	timezones0.put("Europe/Brussels", tz);
	timezones0.put("Europe/Budapest", tz);
	timezones0.put("Europe/Copenhagen", tz);
	timezones0.put("Europe/Gibraltar", tz);
	timezones0.put("Europe/Ljubljana", tz);
	timezones0.put("Europe/Luxembourg", tz);
	timezones0.put("Europe/Madrid", tz);
	timezones0.put("Europe/Malta", tz);
	timezones0.put("Europe/Monaco", tz);
	timezones0.put("Europe/Oslo", tz);
	timezones0.put("Europe/Paris", tz);
	timezones0.put("Europe/Prague", tz);
	timezones0.put("Europe/Rome", tz);
	timezones0.put("Europe/San_Marino", tz);
	timezones0.put("Europe/Sarajevo", tz);
	timezones0.put("Europe/Skopje", tz);
	timezones0.put("Europe/Stockholm", tz);
	timezones0.put("Europe/Tirane", tz);
	timezones0.put("Europe/Vaduz", tz);
	timezones0.put("Europe/Vatican", tz);
	timezones0.put("Europe/Vienna", tz);
	timezones0.put("Europe/Warsaw", tz);
	timezones0.put("Europe/Zagreb", tz);
	timezones0.put("Europe/Zurich", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "ART",
	   Calendar.APRIL, -1, Calendar.FRIDAY, 0 * 3600,
	   Calendar.SEPTEMBER, -1, Calendar.THURSDAY, 23000 * 3600);
	timezones0.put("ART", tz);
	timezones0.put("Africa/Cairo", tz);
	tz = new SimpleTimeZone(2000 * 3600, "CAT");
	timezones0.put("CAT", tz);
	timezones0.put("Africa/Blantyre", tz);
	timezones0.put("Africa/Bujumbura", tz);
	timezones0.put("Africa/Gaborone", tz);
	timezones0.put("Africa/Harare", tz);
	timezones0.put("Africa/Johannesburg", tz);
	timezones0.put("Africa/Kigali", tz);
	timezones0.put("Africa/Lubumbashi", tz);
	timezones0.put("Africa/Lusaka", tz);
	timezones0.put("Africa/Maputo", tz);
	timezones0.put("Africa/Maseru", tz);
	timezones0.put("Africa/Mbabane", tz);
	timezones0.put("Africa/Tripoli", tz);
	timezones0.put("Europe/Riga", tz);
	timezones0.put("Europe/Tallinn", tz);
	timezones0.put("Europe/Vilnius", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "Asia/Amman",
	   Calendar.MARCH, -1, Calendar.THURSDAY, 0 * 3600,
	   Calendar.SEPTEMBER, -1, Calendar.THURSDAY, 0 * 3600);
	timezones0.put("Asia/Amman", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "Asia/Beirut",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("Asia/Beirut", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "Asia/Damascus",
	   Calendar.APRIL, 1, 0, 0 * 3600,
	   Calendar.OCTOBER, 1, 0, 0 * 3600);
	timezones0.put("Asia/Damascus", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "Asia/Gaza",
	   Calendar.APRIL, 3, Calendar.FRIDAY, 0 * 3600,
	   Calendar.OCTOBER, 3, Calendar.FRIDAY, 0 * 3600);
	timezones0.put("Asia/Gaza", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "Asia/Jerusalem",
	   Calendar.APRIL, 1, 0, 1000 * 3600,
	   Calendar.OCTOBER, 1, 0, 1000 * 3600);
	timezones0.put("Asia/Jerusalem", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "EET",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 3000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 3000 * 3600);
	timezones0.put("EET", tz);
	timezones0.put("Asia/Istanbul", tz);
	timezones0.put("Asia/Nicosia", tz);
	timezones0.put("Europe/Athens", tz);
	timezones0.put("Europe/Bucharest", tz);
	timezones0.put("Europe/Chisinau", tz);
	timezones0.put("Europe/Helsinki", tz);
	timezones0.put("Europe/Istanbul", tz);
	timezones0.put("Europe/Kiev", tz);
	timezones0.put("Europe/Nicosia", tz);
	timezones0.put("Europe/Simferopol", tz);
	timezones0.put("Europe/Sofia", tz);
	timezones0.put("Europe/Uzhgorod", tz);
	timezones0.put("Europe/Zaporozhye", tz);
	tz = new SimpleTimeZone
	  (2000 * 3600, "Europe/Kaliningrad",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Europe/Kaliningrad", tz);
	timezones0.put("Europe/Minsk", tz);
	tz = new SimpleTimeZone
	  (3000 * 3600, "Asia/Baghdad",
	   Calendar.APRIL, 1, 0, 3000 * 3600,
	   Calendar.OCTOBER, 1, 0, 3000 * 3600);
	timezones0.put("Asia/Baghdad", tz);
	tz = new SimpleTimeZone
	  (3000 * 3600, "Europe/Moscow",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Europe/Moscow", tz);
	timezones0.put("Europe/Tiraspol", tz);
	tz = new SimpleTimeZone(3000 * 3600, "EAT");
	timezones0.put("EAT", tz);
	timezones0.put("Africa/Addis_Ababa", tz);
	timezones0.put("Africa/Asmera", tz);
	timezones0.put("Africa/Dar_es_Salaam", tz);
	timezones0.put("Africa/Djibouti", tz);
	timezones0.put("Africa/Kampala", tz);
	timezones0.put("Africa/Khartoum", tz);
	timezones0.put("Africa/Mogadishu", tz);
	timezones0.put("Africa/Nairobi", tz);
	timezones0.put("Antarctica/Syowa", tz);
	timezones0.put("Asia/Aden", tz);
	timezones0.put("Asia/Bahrain", tz);
	timezones0.put("Asia/Kuwait", tz);
	timezones0.put("Asia/Qatar", tz);
	timezones0.put("Asia/Riyadh", tz);
	timezones0.put("Indian/Antananarivo", tz);
	timezones0.put("Indian/Comoro", tz);
	timezones0.put("Indian/Mayotte", tz);
	tz = new SimpleTimeZone(3500 * 3600, "Asia/Tehran");
	timezones0.put("Asia/Tehran", tz);
	tz = new SimpleTimeZone
	  (4000 * 3600, "Asia/Baku",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 1000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 1000 * 3600);
	timezones0.put("Asia/Baku", tz);
	tz = new SimpleTimeZone
	  (4000 * 3600, "Asia/Aqtau",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("Asia/Aqtau", tz);
	timezones0.put("Asia/Tbilisi", tz);
	tz = new SimpleTimeZone
	  (4000 * 3600, "Asia/Yerevan",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Yerevan", tz);
	timezones0.put("Europe/Samara", tz);
	tz = new SimpleTimeZone(4000 * 3600, "NET");
	timezones0.put("NET", tz);
	timezones0.put("Asia/Dubai", tz);
	timezones0.put("Asia/Muscat", tz);
	timezones0.put("Indian/Mahe", tz);
	timezones0.put("Indian/Mauritius", tz);
	timezones0.put("Indian/Reunion", tz);
	tz = new SimpleTimeZone(4500 * 3600, "Asia/Kabul");
	timezones0.put("Asia/Kabul", tz);
	tz = new SimpleTimeZone
	  (5000 * 3600, "Asia/Aqtobe",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("Asia/Aqtobe", tz);
	tz = new SimpleTimeZone
	  (5000 * 3600, "Asia/Bishkek",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2500 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2500 * 3600);
	timezones0.put("Asia/Bishkek", tz);
	tz = new SimpleTimeZone
	  (5000 * 3600, "Asia/Yekaterinburg",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Yekaterinburg", tz);
	tz = new SimpleTimeZone(5000 * 3600, "PLT");
	timezones0.put("PLT", tz);
	timezones0.put("Asia/Ashgabat", tz);
	timezones0.put("Asia/Dushanbe", tz);
	timezones0.put("Asia/Karachi", tz);
	timezones0.put("Asia/Samarkand", tz);
	timezones0.put("Asia/Tashkent", tz);
	timezones0.put("Indian/Chagos", tz);
	timezones0.put("Indian/Kerguelen", tz);
	timezones0.put("Indian/Maldives", tz);
	tz = new SimpleTimeZone(5500 * 3600, "IST");
	timezones0.put("IST", tz);
	timezones0.put("Asia/Calcutta", tz);
	tz = new SimpleTimeZone(5750 * 3600, "Asia/Katmandu");
	timezones0.put("Asia/Katmandu", tz);
	tz = new SimpleTimeZone(6000 * 3600, "BST");
	timezones0.put("BST", tz);
	timezones0.put("Antarctica/Mawson", tz);
	timezones0.put("Asia/Colombo", tz);
	timezones0.put("Asia/Dhaka", tz);
	timezones0.put("Asia/Thimphu", tz);
	tz = new SimpleTimeZone
	  (6000 * 3600, "Asia/Almaty",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 0 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 0 * 3600);
	timezones0.put("Asia/Almaty", tz);
	tz = new SimpleTimeZone
	  (6000 * 3600, "Asia/Novosibirsk",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Novosibirsk", tz);
	timezones0.put("Asia/Omsk", tz);
	tz = new SimpleTimeZone(6500 * 3600, "Asia/Rangoon");
	timezones0.put("Asia/Rangoon", tz);
	timezones0.put("Indian/Cocos", tz);
	tz = new SimpleTimeZone(7000 * 3600, "VST");
	timezones0.put("VST", tz);
	timezones0.put("Antarctica/Davis", tz);
	timezones0.put("Asia/Bangkok", tz);
	timezones0.put("Asia/Hovd", tz);
	timezones0.put("Asia/Jakarta", tz);
	timezones0.put("Asia/Phnom_Penh", tz);
	timezones0.put("Asia/Saigon", tz);
	timezones0.put("Asia/Vientiane", tz);
	timezones0.put("Indian/Christmas", tz);
	tz = new SimpleTimeZone
	  (7000 * 3600, "Asia/Krasnoyarsk",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Krasnoyarsk", tz);
	tz = new SimpleTimeZone(8000 * 3600, "CTT");
	timezones0.put("CTT", tz);
	timezones0.put("Antarctica/Casey", tz);
	timezones0.put("Asia/Brunei", tz);
	timezones0.put("Asia/Chungking", tz);
	timezones0.put("Asia/Harbin", tz);
	timezones0.put("Asia/Hong_Kong", tz);
	timezones0.put("Asia/Kashgar", tz);
	timezones0.put("Asia/Kuala_Lumpur", tz);
	timezones0.put("Asia/Kuching", tz);
	timezones0.put("Asia/Macao", tz);
	timezones0.put("Asia/Manila", tz);
	timezones0.put("Asia/Shanghai", tz);
	timezones0.put("Asia/Singapore", tz);
	timezones0.put("Asia/Taipei", tz);
	timezones0.put("Asia/Ujung_Pandang", tz);
	timezones0.put("Asia/Ulaanbaatar", tz);
	timezones0.put("Asia/Urumqi", tz);
	timezones0.put("Australia/Perth", tz);
	tz = new SimpleTimeZone
	  (8000 * 3600, "Asia/Irkutsk",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Irkutsk", tz);
	tz = new SimpleTimeZone(9000 * 3600, "JST");
	timezones0.put("JST", tz);
	timezones0.put("Asia/Dili", tz);
	timezones0.put("Asia/Jayapura", tz);
	timezones0.put("Asia/Pyongyang", tz);
	timezones0.put("Asia/Seoul", tz);
	timezones0.put("Asia/Tokyo", tz);
	timezones0.put("Pacific/Palau", tz);
	tz = new SimpleTimeZone
	  (9000 * 3600, "Asia/Yakutsk",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Yakutsk", tz);
	tz = new SimpleTimeZone
	  (9500 * 3600, "Australia/Adelaide",
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Australia/Adelaide", tz);
	timezones0.put("Australia/Broken_Hill", tz);
	tz = new SimpleTimeZone(9500 * 3600, "ACT");
	timezones0.put("ACT", tz);
	timezones0.put("Australia/Darwin", tz);
	tz = new SimpleTimeZone(10000 * 3600, "Antarctica/DumontDUrville");
	timezones0.put("Antarctica/DumontDUrville", tz);
	timezones0.put("Australia/Brisbane", tz);
	timezones0.put("Australia/Lindeman", tz);
	timezones0.put("Pacific/Guam", tz);
	timezones0.put("Pacific/Port_Moresby", tz);
	timezones0.put("Pacific/Saipan", tz);
	timezones0.put("Pacific/Truk", tz);
	timezones0.put("Pacific/Yap", tz);
	tz = new SimpleTimeZone
	  (10000 * 3600, "Asia/Vladivostok",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Vladivostok", tz);
	tz = new SimpleTimeZone
	  (10000 * 3600, "Australia/Hobart",
	   Calendar.OCTOBER, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Australia/Hobart", tz);
	tz = new SimpleTimeZone
	  (10000 * 3600, "AET",
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("AET", tz);
	timezones0.put("Australia/Melbourne", tz);
	timezones0.put("Australia/Sydney", tz);
	tz = new SimpleTimeZone
	  (10500 * 3600, "Australia/Lord_Howe",
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600, 500 * 3600);
	timezones0.put("Australia/Lord_Howe", tz);
	tz = new SimpleTimeZone
	  (11000 * 3600, "Asia/Magadan",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Magadan", tz);
	tz = new SimpleTimeZone(11000 * 3600, "SST");
	timezones0.put("SST", tz);
	timezones0.put("Pacific/Efate", tz);
	timezones0.put("Pacific/Guadalcanal", tz);
	timezones0.put("Pacific/Kosrae", tz);
	timezones0.put("Pacific/Noumea", tz);
	timezones0.put("Pacific/Ponape", tz);
	tz = new SimpleTimeZone(11500 * 3600, "Pacific/Norfolk");
	timezones0.put("Pacific/Norfolk", tz);
	tz = new SimpleTimeZone
	  (12000 * 3600, "NST",
	   Calendar.OCTOBER, 1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.MARCH, 3, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("NST", tz);
	timezones0.put("Antarctica/McMurdo", tz);
	timezones0.put("Antarctica/South_Pole", tz);
	timezones0.put("Pacific/Auckland", tz);
	tz = new SimpleTimeZone
	  (12000 * 3600, "Asia/Anadyr",
	   Calendar.MARCH, -1, Calendar.SUNDAY, 2000 * 3600,
	   Calendar.OCTOBER, -1, Calendar.SUNDAY, 2000 * 3600);
	timezones0.put("Asia/Anadyr", tz);
	timezones0.put("Asia/Kamchatka", tz);
	tz = new SimpleTimeZone(12000 * 3600, "Pacific/Fiji");
	timezones0.put("Pacific/Fiji", tz);
	timezones0.put("Pacific/Funafuti", tz);
	timezones0.put("Pacific/Kwajalein", tz);
	timezones0.put("Pacific/Majuro", tz);
	timezones0.put("Pacific/Nauru", tz);
	timezones0.put("Pacific/Tarawa", tz);
	timezones0.put("Pacific/Wake", tz);
	timezones0.put("Pacific/Wallis", tz);
	tz = new SimpleTimeZone
	  (12750 * 3600, "Pacific/Chatham",
	   Calendar.OCTOBER, 1, Calendar.SUNDAY, 2750 * 3600,
	   Calendar.MARCH, 3, Calendar.SUNDAY, 2750 * 3600);
	timezones0.put("Pacific/Chatham", tz);
	tz = new SimpleTimeZone(13000 * 3600, "Pacific/Enderbury");
	timezones0.put("Pacific/Enderbury", tz);
	timezones0.put("Pacific/Tongatapu", tz);
	tz = new SimpleTimeZone(14000 * 3600, "Pacific/Kiritimati");
	timezones0.put("Pacific/Kiritimati", tz);
      }
    return timezones0;
  }

  /**
   * This method returns a time zone id string which is in the form
   * (standard zone name) or (standard zone name)(GMT offset) or
   * (standard zone name)(GMT offset)(daylight time zone name).  The
   * GMT offset can be in seconds, or where it is evenly divisible by
   * 3600, then it can be in hours.  The offset must be the time to
   * add to the local time to get GMT.  If a offset is given and the
   * time zone observes daylight saving then the (daylight time zone
   * name) must also be given (otherwise it is assumed the time zone
   * does not observe any daylight savings).
   * <p>
   * The result of this method is given to getDefaultTimeZone(String)
   * which tries to map the time zone id to a known TimeZone.  See
   * that method on how the returned String is mapped to a real
   * TimeZone object.
   */
  private static native String getDefaultTimeZoneId();

  /**
   * Tries to read the time zone name from a file. Only the first
   * consecutive letters, digits, slashes, dashes and underscores are
   * read from the file. If the file cannot be read or an IOException
   * occurs null is returned.
   * <p>
   * The /etc/timezone file is not standard, but a lot of systems have
   * it. If it exist the first line always contains a string
   * describing the timezone of the host of domain. Some systems
   * contain a /etc/TIMEZONE file which is used to set the TZ
   * environment variable (which is checked before /etc/timezone is
   * read).
   */
  private static String readTimeZoneFile(String file)
  {
    File f = new File(file);
    if (!f.exists())
      return null;

    InputStreamReader isr = null;
    try
      {
	FileInputStream fis = new FileInputStream(f);
	BufferedInputStream bis = new BufferedInputStream(fis);
	isr = new InputStreamReader(bis);
	
	StringBuffer sb = new StringBuffer();
	int i = isr.read();
	while (i != -1)
	  {
	    char c = (char) i;
	    if (Character.isLetter(c) || Character.isDigit(c)
		|| c == '/' || c == '-' || c == '_')
	      {
		sb.append(c);
		i = isr.read();
	      }
	    else
	      break;
	  }
	return sb.toString();
      }
    catch (IOException ioe)
      {
	// Parse error, not a proper tzfile.
	return null;
      }
    finally
      {
	try
	  {
	    if (isr != null)
	      isr.close();
	  }
	catch (IOException ioe)
	  {
	    // Error while close, nothing we can do.
	  }
      }
  }

  /**
   * Tries to read a file as a "standard" tzfile and return a time
   * zone id string as expected by <code>getDefaultTimeZone(String)</code>.
   * If the file doesn't exist, an IOException occurs or it isn't a tzfile
   * that can be parsed null is returned.
   * <p>
   * The tzfile structure (as also used by glibc) is described in the Olson
   * tz database archive as can be found at
   * <code>ftp://elsie.nci.nih.gov/pub/</code>.
   * <p>
   * At least the following platforms support the tzdata file format
   * and /etc/localtime (GNU/Linux, Darwin, Solaris and FreeBSD at
   * least). Some systems (like Darwin) don't start the file with the
   * required magic bytes 'TZif', this implementation can handle
   * that).
   */
  private static String readtzFile(String file)
  {
    File f = new File(file);
    if (!f.exists())
      return null;
    
    DataInputStream dis = null;
    try
      {
        FileInputStream fis = new FileInputStream(f);
        BufferedInputStream bis = new BufferedInputStream(fis);
        dis = new DataInputStream(bis);
	
        // Make sure we are reading a tzfile.
        byte[] tzif = new byte[4];
        dis.readFully(tzif);
        if (tzif[0] == 'T' && tzif[1] == 'Z'
            && tzif[2] == 'i' && tzif[3] == 'f')
	  // Reserved bytes, ttisgmtcnt, ttisstdcnt and leapcnt
	  skipFully(dis, 16 + 3 * 4);
	else
	  // Darwin has tzdata files that don't start with the TZif marker
	  skipFully(dis, 16 + 3 * 4 - 4);
	
	int timecnt = dis.readInt();
	int typecnt = dis.readInt();
	if (typecnt > 0)
	  {
	    int charcnt = dis.readInt();
	    // Transition times plus indexed transition times.
	    skipFully(dis, timecnt * (4 + 1));
	    
	    // Get last gmt_offset and dst/non-dst time zone names.
	    int abbrind = -1;
	    int dst_abbrind = -1;
	    int gmt_offset = 0;
	    while (typecnt-- > 0)
	      {
		// gmtoff
		int offset = dis.readInt();
		int dst = dis.readByte();
		if (dst == 0)
		  {
		    abbrind = dis.readByte();
		    gmt_offset = offset;
		  }
		else
		  dst_abbrind = dis.readByte();
	      }
	    
	    // gmt_offset is the offset you must add to UTC/GMT to
	    // get the local time, we need the offset to add to
	    // the local time to get UTC/GMT.
	    gmt_offset *= -1;
	    
	    // Turn into hours if possible.
	    if (gmt_offset % 3600 == 0)
	      gmt_offset /= 3600;
	    
	    if (abbrind >= 0)
	      {
		byte[] names = new byte[charcnt];
		dis.readFully(names);
		int j = abbrind;
		while (j < charcnt && names[j] != 0)
		  j++;
		
		String zonename = new String(names, abbrind, j - abbrind,
					     "ASCII");
		
		String dst_zonename;
		if (dst_abbrind >= 0)
		  {
		    j = dst_abbrind;
		    while (j < charcnt && names[j] != 0)
		      j++;
		    dst_zonename = new String(names, dst_abbrind,
					      j - dst_abbrind, "ASCII");
		  }
		else
		  dst_zonename = "";
		
		// Only use gmt offset when necessary.
		// Also special case GMT+/- timezones.
		String offset_string;
		if ("".equals(dst_zonename)
		    && (gmt_offset == 0
			|| zonename.startsWith("GMT+")
			|| zonename.startsWith("GMT-")))
		  offset_string = "";
		else
		  offset_string = Integer.toString(gmt_offset);
		
		String id = zonename + offset_string + dst_zonename;
		
		return id;
	      }
	  }
	
	// Something didn't match while reading the file.
	return null;
      }
    catch (IOException ioe)
      {
	// Parse error, not a proper tzfile.
	return null;
      }
    finally
      {
	try
	  {
	    if (dis != null)
	      dis.close();
	  }
	catch(IOException ioe)
	  {
	    // Error while close, nothing we can do.
	  }
      }
  }
  
  /**
   * Skips the requested number of bytes in the given InputStream.
   * Throws EOFException if not enough bytes could be skipped.
   * Negative numbers of bytes to skip are ignored.
   */
  private static void skipFully(InputStream is, long l) throws IOException
  {
    while (l > 0)
      {
        long k = is.skip(l);
        if (k <= 0)
          throw new EOFException();
        l -= k;
      }
  }
  
  /**
   * Maps a time zone name (with optional GMT offset and daylight time
   * zone name) to one of the known time zones.  This method called
   * with the result of <code>System.getProperty("user.timezone")</code>
   * or <code>getDefaultTimeZoneId()</code>.  Note that giving one of
   * the standard tz data names from ftp://elsie.nci.nih.gov/pub/ is
   * preferred.  The time zone name can be given as follows:
   * <code>(standard zone name)[(GMT offset)[(daylight time zone name)]]</code>
   * <p>
   * If only a (standard zone name) is given (no numbers in the
   * String) then it gets mapped directly to the TimeZone with that
   * name, if that fails null is returned.
   * <p>
   * A GMT offset is the offset to add to the local time to get GMT.
   * If a (GMT offset) is included (either in seconds or hours) then
   * an attempt is made to find a TimeZone name matching both the name
   * and the offset (that doesn't observe daylight time, if the
   * timezone observes daylight time then you must include a daylight
   * time zone name after the offset), if that fails then a TimeZone
   * with the given GMT offset is returned (whether or not the
   * TimeZone observes daylight time is ignored), if that also fails
   * the GMT TimeZone is returned.
   * <p>
   * If the String ends with (GMT offset)(daylight time zone name)
   * then an attempt is made to find a TimeZone with the given name and
   * GMT offset that also observes (the daylight time zone name is not
   * currently used in any other way), if that fails a TimeZone with
   * the given GMT offset that observes daylight time is returned, if
   * that also fails the GMT TimeZone is returned.
   * <p>
   * Examples: In Chicago, the time zone id could be "CST6CDT", but
   * the preferred name would be "America/Chicago".  In Indianapolis
   * (which does not have Daylight Savings Time) the string could be
   * "EST5", but the preferred name would be "America/Indianapolis".
   * The standard time zone name for The Netherlands is "Europe/Amsterdam",
   * but can also be given as "CET-1CEST".
   */
  private static TimeZone getDefaultTimeZone(String sysTimeZoneId)
  {
    // First find start of GMT offset info and any Daylight zone name.
    int startGMToffset = 0;
    int sysTimeZoneIdLength = sysTimeZoneId.length();
    for (int i = 0; i < sysTimeZoneIdLength && startGMToffset == 0; i++)
      {
	char c = sysTimeZoneId.charAt(i);
	if (c == '+' || c == '-' || Character.isDigit(c))
	  startGMToffset = i;
      }
    
    String tzBasename;
    if (startGMToffset == 0)
      tzBasename = sysTimeZoneId;
    else
      tzBasename = sysTimeZoneId.substring (0, startGMToffset);
    
    int startDaylightZoneName = 0;
    for (int i = sysTimeZoneIdLength - 1;
	 i >= 0 && !Character.isDigit(sysTimeZoneId.charAt(i)); --i)
      startDaylightZoneName = i;
    
    boolean useDaylightTime = startDaylightZoneName > 0;
    
    // Integer.parseInt() doesn't handle leading +.
    if (sysTimeZoneId.charAt(startGMToffset) == '+')
      startGMToffset++;
    
    int gmtOffset = 0;
    if (startGMToffset > 0)
      {
	gmtOffset = Integer.parseInt
	  (startDaylightZoneName == 0
	   ? sysTimeZoneId.substring(startGMToffset)
	   : sysTimeZoneId.substring(startGMToffset,
				     startDaylightZoneName));
	
	// Offset could be in hours or seconds.  Convert to millis.
	// The offset is given as the time to add to local time to get GMT
	// we need the time to add to GMT to get localtime.
	if (gmtOffset < 24)
	  gmtOffset *= 60 * 60;
	gmtOffset *= -1000;
      }
    
    // Try to be optimistic and get the timezone that matches the base name.
    // If we only have the base name then just accept this timezone.
    // Otherwise check the gmtOffset and day light attributes.
    TimeZone tz = (TimeZone) timezones().get(tzBasename);
    if (tz != null
	&& (tzBasename == sysTimeZoneId
	    || (tz.getRawOffset() == gmtOffset
		&& tz.useDaylightTime() == useDaylightTime)))
      return tz;
    
    // Maybe there is one with the daylight zone name?
    if (useDaylightTime)
      {
	String daylightZoneName;
	daylightZoneName = sysTimeZoneId.substring(startDaylightZoneName);
	if (!daylightZoneName.equals(tzBasename))
	  {
	    tz = (TimeZone) timezones().get(tzBasename);
	    if (tz != null
		&& tz.getRawOffset() == gmtOffset
		&& tz.useDaylightTime())
	      return tz;
	  }
      }
    
    // If no match, see if a valid timezone has similar attributes as this
    // and then use it instead. We take the first one that looks OKish.
    if (startGMToffset > 0)
      {
	String[] ids = getAvailableIDs(gmtOffset);
	for (int i = 0; i < ids.length; i++)
	  {
	    tz = (TimeZone) timezones().get(ids[i]);
	    if (tz.useDaylightTime() == useDaylightTime)
	      return tz;
	  }
      }
    
    return null;
  }

  /**
   * Gets the time zone offset, for current date, modified in case of 
   * daylight savings.  This is the offset to add to UTC to get the local
   * time.
   * @param era the era of the given date
   * @param year the year of the given date
   * @param month the month of the given date, 0 for January.
   * @param day the day of month
   * @param dayOfWeek the day of week
   * @param milliseconds the millis in the day (in local standard time)
   * @return the time zone offset in milliseconds.
   */
  public abstract int getOffset(int era, int year, int month,
				int day, int dayOfWeek, int milliseconds);

  /**
   * Get the time zone offset for the specified date, modified in case of
   * daylight savings.  This is the offset to add to UTC to get the local
   * time.
   * @param date the date represented in millisecends
   * since January 1, 1970 00:00:00 GMT.
   * @since 1.4
   */
  public int getOffset(long date)
  {
    return (inDaylightTime(new Date(date))
            ? getRawOffset() + getDSTSavings()
            : getRawOffset());
  }
  
  /**
   * Gets the time zone offset, ignoring daylight savings.  This is
   * the offset to add to UTC to get the local time.
   * @return the time zone offset in milliseconds.  
   */
  public abstract int getRawOffset();

  /**
   * Sets the time zone offset, ignoring daylight savings.  This is
   * the offset to add to UTC to get the local time.
   * @param offsetMillis the time zone offset to GMT.
   */
  public abstract void setRawOffset(int offsetMillis);

  /**
   * Gets the identifier of this time zone. For instance, PST for
   * Pacific Standard Time.
   * @returns the ID of this time zone.  
   */
  public String getID()
  {
    return ID;
  }

  /**
   * Sets the identifier of this time zone. For instance, PST for
   * Pacific Standard Time.
   * @param id the new time zone ID.
   */
  public void setID(String id)
  {
    this.ID = id;
  }

  /**
   * This method returns a string name of the time zone suitable
   * for displaying to the user.  The string returned will be the long
   * description of the timezone in the current locale.  The name
   * displayed will assume daylight savings time is not in effect.
   *
   * @return The name of the time zone.
   */
  public final String getDisplayName()
  {
    return (getDisplayName(false, LONG, Locale.getDefault()));
  }

  /**
   * This method returns a string name of the time zone suitable
   * for displaying to the user.  The string returned will be the long
   * description of the timezone in the specified locale. The name
   * displayed will assume daylight savings time is not in effect.
   *
   * @param locale The locale for this timezone name.
   *
   * @return The name of the time zone.
   */
  public final String getDisplayName(Locale locale)
  {
    return (getDisplayName(false, LONG, locale));
  }

  /**
   * This method returns a string name of the time zone suitable
   * for displaying to the user.  The string returned will be of the
   * specified type in the current locale. 
   *
   * @param dst Whether or not daylight savings time is in effect.
   * @param style <code>LONG</code> for a long name, <code>SHORT</code> for
   * a short abbreviation.
   *
   * @return The name of the time zone.
   */
  public final String getDisplayName(boolean dst, int style)
  {
    return (getDisplayName(dst, style, Locale.getDefault()));
  }


  /**
   * This method returns a string name of the time zone suitable
   * for displaying to the user.  The string returned will be of the
   * specified type in the specified locale. 
   *
   * @param dst Whether or not daylight savings time is in effect.
   * @param style <code>LONG</code> for a long name, <code>SHORT</code> for
   * a short abbreviation.
   * @param locale The locale for this timezone name.
   *
   * @return The name of the time zone.
   */
  public String getDisplayName(boolean dst, int style, Locale locale)
  {
    DateFormatSymbols dfs;
    try
      {
	dfs = new DateFormatSymbols(locale);

	// The format of the value returned is defined by us.
	String[][]zoneinfo = dfs.getZoneStrings();
	for (int i = 0; i < zoneinfo.length; i++)
	  {
	    if (zoneinfo[i][0].equals(getID()))
	      {
		if (!dst)
		  {
		    if (style == SHORT)
		      return (zoneinfo[i][2]);
		    else
		      return (zoneinfo[i][1]);
		  }
		else
		  {
		    if (style == SHORT)
		      return (zoneinfo[i][4]);
		    else
		      return (zoneinfo[i][3]);
		  }
	      }
	  }
      }
    catch (MissingResourceException e)
      {
      }

    return getDefaultDisplayName(dst);
  }

  private String getDefaultDisplayName(boolean dst)
  {
    int offset = getRawOffset();
    if (dst && this instanceof SimpleTimeZone)
      {
	// ugly, but this is a design failure of the API:
	// getDisplayName takes a dst parameter even though
	// TimeZone knows nothing about daylight saving offsets.
	offset += ((SimpleTimeZone) this).getDSTSavings();
      }

    StringBuffer sb = new StringBuffer(9);
    sb.append("GMT");
    sb.append(offset >= 0 ? '+' : '-');

    offset = Math.abs(offset) / (1000 * 60);
    int hours = offset / 60;
    int minutes = offset % 60;

    sb.append((char) ('0' + hours / 10)).append((char) ('0' + hours % 10));
    sb.append(':');
    sb.append((char) ('0' + minutes / 10)).append((char) ('0' + minutes % 10));
    return sb.toString();
  }

  /** 
   * Returns true, if this time zone uses Daylight Savings Time.
   */
  public abstract boolean useDaylightTime();

  /**
   * Returns true, if the given date is in Daylight Savings Time in this
   * time zone.
   * @param date the given Date.
   */
  public abstract boolean inDaylightTime(Date date);

  /**
   * Gets the daylight savings offset.  This is a positive offset in
   * milliseconds with respect to standard time.  Typically this
   * is one hour, but for some time zones this may be half an our.
   * <p>The default implementation returns 3600000 milliseconds
   * (one hour) if the time zone uses daylight savings time
   * (as specified by {@link #useDaylightTime()}), otherwise
   * it returns 0.
   * @return the daylight savings offset in milliseconds.
   * @since 1.4
   */
  public int getDSTSavings ()
  {
    return useDaylightTime () ? 3600000 : 0;
  }

  /**
   * Gets the TimeZone for the given ID.
   * @param ID the time zone identifier.
   * @return The time zone for the identifier or GMT, if no such time
   * zone exists.
   */
  // FIXME: XXX: JCL indicates this and other methods are synchronized.
  public static TimeZone getTimeZone(String ID)
  {
    // First check timezones hash
    TimeZone tz = (TimeZone) timezones().get(ID);
    if (tz != null)
      {
	if (tz.getID().equals(ID))
	  return tz;

	// We always return a timezone with the requested ID.
	// This is the same behaviour as with JDK1.2.
	tz = (TimeZone) tz.clone();
	tz.setID(ID);
	// We also save the alias, so that we return the same
	// object again if getTimeZone is called with the same
	// alias.
	timezones().put(ID, tz);
	return tz;
      }

    // See if the ID is really a GMT offset form.
    // Note that GMT is in the table so we know it is different.
    if (ID.startsWith("GMT"))
      {
	int pos = 3;
	int offset_direction = 1;

	if (ID.charAt(pos) == '-')
	  {
	    offset_direction = -1;
	    pos++;
	  }
	else if (ID.charAt(pos) == '+')
	  {
	    pos++;
	  }

	try
	  {
	    int hour, minute;

	    String offset_str = ID.substring(pos);
	    int idx = offset_str.indexOf(":");
	    if (idx != -1)
	      {
		hour = Integer.parseInt(offset_str.substring(0, idx));
		minute = Integer.parseInt(offset_str.substring(idx + 1));
	      }
	    else
	      {
		int offset_length = offset_str.length();
		if (offset_length <= 2)
		  {
		    // Only hour
		    hour = Integer.parseInt(offset_str);
		    minute = 0;
		  }
		else
		  {
		    // hour and minute, not separated by colon
		    hour = Integer.parseInt
		      (offset_str.substring(0, offset_length - 2));
		    minute = Integer.parseInt
		      (offset_str.substring(offset_length - 2));
		  }
	      }

	    return new SimpleTimeZone((hour * (60 * 60 * 1000) +
				       minute * (60 * 1000))
				      * offset_direction, ID);
	  }
	catch (NumberFormatException e)
	  {
	  }
      }

    // Finally, return GMT per spec
    return getTimeZone("GMT");
  }

  /**
   * Gets the available IDs according to the given time zone
   * offset.  
   * @param rawOffset the given time zone GMT offset.
   * @return An array of IDs, where the time zone has the specified GMT
   * offset. For example <code>{"Phoenix", "Denver"}</code>, since both have
   * GMT-07:00, but differ in daylight savings behaviour.
   */
  public static String[] getAvailableIDs(int rawOffset)
  {
    int count = 0;
    Iterator iter = timezones().entrySet().iterator();
    while (iter.hasNext())
      {
	// Don't iterate the values, since we want to count 
	// doubled values (aliases)
	Map.Entry entry = (Map.Entry) iter.next();
	if (((TimeZone) entry.getValue()).getRawOffset() == rawOffset)
	  count++;
      }

    String[] ids = new String[count];
    count = 0;
    iter = timezones().entrySet().iterator();
    while (iter.hasNext())
      {
	Map.Entry entry = (Map.Entry) iter.next();
	if (((TimeZone) entry.getValue()).getRawOffset() == rawOffset)
	  ids[count++] = (String) entry.getKey();
      }
    return ids;
  }

  /**
   * Gets all available IDs.
   * @return An array of all supported IDs.
   */
  public static String[] getAvailableIDs()
  {
    return (String[])
      timezones().keySet().toArray(new String[timezones().size()]);
  }

  /**
   * Returns the time zone under which the host is running.  This
   * can be changed with setDefault.
   *
   * @return A clone of the current default time zone for this host.
   * @see #setDefault
   */
  public static TimeZone getDefault()
  {
    return (TimeZone) defaultZone().clone();
  }

  public static void setDefault(TimeZone zone)
  {
    // Hmmmm. No Security checks?
    defaultZone0 = zone;
  }

  /**
   * Test if the other time zone uses the same rule and only
   * possibly differs in ID.  This implementation for this particular
   * class will return true if the raw offsets are identical.  Subclasses
   * should override this method if they use daylight savings.
   * @return true if this zone has the same raw offset
   */
  public boolean hasSameRules(TimeZone other)
  {
    return other.getRawOffset() == getRawOffset();
  }

  /**
   * Returns a clone of this object.  I can't imagine, why this is
   * useful for a time zone.
   */
  public Object clone()
  {
    try
      {
	return super.clone();
      }
    catch (CloneNotSupportedException ex)
      {
	return null;
      }
  }
}
