/* LocaleInformation_be_BY.java
   Copyright (C) 2002 Free Software Foundation, Inc.

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


// This file was automatically generated by localedef.

package gnu.java.locale;

import java.util.ListResourceBundle;

public class LocaleInformation_be_BY extends ListResourceBundle
{
  static final String decimalSeparator = ",";
  static final String groupingSeparator = ".";
  static final String numberFormat = "#,###,##0.###";
  static final String percentFormat = "#,###,##0%";
  static final String[] weekdays = { null, "\u041D\u044F\u0434\u0437\u0435\u043B\u044F", "\u041F\u0430\u043D\u044F\u0434\u0437\u0435\u043B\u0430\u043A", "\u0410\u045E\u0442\u043E\u0440\u0430\u043A", "\u0421\u0435\u0440\u0430\u0434\u0430", "\u0427\u0430\u0446\u0432\u0435\u0440", "\u041F\u044F\u0442\u043D\u0456\u0446\u0430", "\u0421\u0443\u0431\u043E\u0442\u0430" };

  static final String[] shortWeekdays = { null, "\u041D\u044F\u0434", "\u041F\u0430\u043D", "\u0410\u045E\u0442", "\u0421\u0440\u0434", "\u0427\u0446\u0432", "\u041F\u044F\u0442", "\u0421\u0443\u0431" };

  static final String[] shortMonths = { "\u0421\u0442\u0434", "\u041B\u044E\u0442", "\u0421\u0430\u043A", "\u041A\u0440\u0441", "\u0422\u0440\u0430", "\u0427\u044D\u0440", "\u041B\u0456\u043F", "\u0416\u043D\u0432", "\u0412\u0440\u0441", "\u041A\u0441\u0442", "\u041B\u0456\u0441", "\u0421\u043D\u0436", null };

  static final String[] months = { "\u0421\u0442\u0443\u0434\u0437\u0435\u043D\u044C", "\u041B\u044E\u0442\u044B", "\u0421\u0430\u043A\u0430\u0432\u0456\u043A", "\u041A\u0440\u0430\u0441\u0430\u0432\u0456\u043A", "\u0422\u0440\u0430\u0432\u0435\u043D\u044C", "\u0427\u044D\u0440\u0432\u0435\u043D\u044C", "\u041B\u0456\u043F\u0435\u043D\u044C", "\u0416\u043D\u0456\u0432\u0435\u043D\u044C", "\u0412\u0435\u0440\u0430\u0441\u0435\u043D\u044C", "\u041A\u0430\u0441\u0442\u0440\u044B\u0447\u043D\u0456\u043A", "\u041B\u0456\u0441\u0442\u0430\u043F\u0430\u0434", "\u0421\u043D\u0435\u0436\u0430\u043D\u044C", null };

  static final String[] ampms = { "", "" };

  static final String shortDateFormat = "dd.MM.yyyy";
  static final String defaultTimeFormat = "";
  static final String currencySymbol = "\u0440\u0443\u0431";
  static final String intlCurrencySymbol = "BYB";
  static final String currencyFormat = "#,###,##0.00 $;-#,###,##0.00 $";

  private static final Object[][] contents =
  {
    { "weekdays", weekdays },
    { "shortWeekdays", shortWeekdays },
    { "shortMonths", shortMonths },
    { "months", months },
    { "ampms", ampms },
    { "shortDateFormat", shortDateFormat },
    { "defaultTimeFormat", defaultTimeFormat },
    { "currencySymbol", currencySymbol },
    { "intlCurrencySymbol", intlCurrencySymbol },
    { "currencyFormat", currencyFormat },
    { "decimalSeparator", decimalSeparator },
    { "groupingSeparator", groupingSeparator },
    { "numberFormat", numberFormat },
    { "percentFormat", percentFormat },
  };

  public Object[][] getContents () { return contents; }
}
