00001 /* gdatetime.c
00002  *
00003  * Copyright (C) 2009-2010 Christian Hergert <chris@dronelabs.com>
00004  * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
00005  * Copyright (C) 2010 Emmanuele Bassi <ebassi@linux.intel.com>
00006  * Copyright ? 2010 Codethink Limited
00007  *
00008  * This library is free software; you can redistribute it and/or modify
00009  * it under the terms of the GNU Lesser General Public License as
00010  * published by the Free Software Foundation; either version 2.1 of the
00011  * licence, or (at your option) any later version.
00012  *
00013  * This is distributed in the hope that it will be useful, but WITHOUT
00014  * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
00015  * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
00016  * License for more details.
00017  *
00018  * You should have received a copy of the GNU Lesser General Public
00019  * License along with this library; if not, write to the Free Software
00020  * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
00021  * USA.
00022  *
00023  * Authors: Christian Hergert <chris@dronelabs.com>
00024  *          Thiago Santos <thiago.sousa.santos@collabora.co.uk>
00025  *          Emmanuele Bassi <ebassi@linux.intel.com>
00026  *          Ryan Lortie <desrt@desrt.ca>
00027  */
00028 
00029 /* Algorithms within this file are based on the Calendar FAQ by
00030  * Claus Tondering.  It can be found at
00031  * http://www.tondering.dk/claus/cal/calendar29.txt
00032  *
00033  * Copyright and disclaimer
00034  * ------------------------
00035  *   This document is Copyright (C) 2008 by Claus Tondering.
00036  *   E-mail: claus@tondering.dk. (Please include the word
00037  *   "calendar" in the subject line.)
00038  *   The document may be freely distributed, provided this
00039  *   copyright notice is included and no money is charged for
00040  *   the document.
00041  *
00042  *   This document is provided "as is". No warranties are made as
00043  *   to its correctness.
00044  */
00045 
00046 /* Prologue {{{1 */
00047 
00048 #include "config.h"
00049 
00050 #include <stdlib.h>
00051 #include <string.h>
00052 
00053 #ifdef HAVE_UNISTD_H
00054 #include <unistd.h>
00055 #endif
00056 
00057 #ifndef G_OS_WIN32
00058 #include <sys/time.h>
00059 #include <time.h>
00060 #endif /* !G_OS_WIN32 */
00061 
00062 #include "gdatetime.h"
00063 
00064 #include "gatomic.h"
00065 #include "gfileutils.h"
00066 #include "ghash.h"
00067 #include "gmain.h"
00068 #include "gmappedfile.h"
00069 #include "gstrfuncs.h"
00070 #include "gtestutils.h"
00071 #include "gthread.h"
00072 #include "gtimezone.h"
00073 
00074 #include "glibintl.h"
00075 
00109 struct _GDateTime
00110 {
00111   /* 1 is 0001-01-01 in Proleptic Gregorian */
00112   gint32 days;
00113 
00114   /* Microsecond timekeeping within Day */
00115   guint64 usec;
00116 
00117   /* TimeZone information */
00118   GTimeZone *tz;
00119   gint interval;
00120 
00121   volatile gint ref_count;
00122 };
00123 
00124 /* Time conversion {{{1 */
00125 
00126 #define UNIX_EPOCH_START     719163
00127 #define INSTANT_TO_UNIX(instant) \
00128   ((instant)/USEC_PER_SECOND - UNIX_EPOCH_START * SEC_PER_DAY)
00129 #define UNIX_TO_INSTANT(unix) \
00130   (((unix) + UNIX_EPOCH_START * SEC_PER_DAY) * USEC_PER_SECOND)
00131 
00132 #define DAYS_IN_4YEARS    1461    /* days in 4 years */
00133 #define DAYS_IN_100YEARS  36524   /* days in 100 years */
00134 #define DAYS_IN_400YEARS  146097  /* days in 400 years  */
00135 
00136 #define USEC_PER_SECOND      (G_GINT64_CONSTANT (1000000))
00137 #define USEC_PER_MINUTE      (G_GINT64_CONSTANT (60000000))
00138 #define USEC_PER_HOUR        (G_GINT64_CONSTANT (3600000000))
00139 #define USEC_PER_MILLISECOND (G_GINT64_CONSTANT (1000))
00140 #define USEC_PER_DAY         (G_GINT64_CONSTANT (86400000000))
00141 #define SEC_PER_DAY          (G_GINT64_CONSTANT (86400))
00142 
00143 #define GREGORIAN_LEAP(y)    ((((y) % 4) == 0) && (!((((y) % 100) == 0) && (((y) % 400) != 0))))
00144 #define JULIAN_YEAR(d)       ((d)->julian / 365.25)
00145 #define DAYS_PER_PERIOD      (G_GINT64_CONSTANT (2914695))
00146 
00147 #define GET_AMPM(d,l)         ((g_date_time_get_hour (d) < 12)  \
00148                                        /* Translators: 'before midday' indicator */ \
00149                                 ? (l ? C_("GDateTime", "am") \
00150                                        /* Translators: 'before midday' indicator */ \
00151                                      : C_("GDateTime", "AM")) \
00152                                   /* Translators: 'after midday' indicator */ \
00153                                 : (l ? C_("GDateTime", "pm") \
00154                                   /* Translators: 'after midday' indicator */ \
00155                                      : C_("GDateTime", "PM")))
00156 
00157 #define WEEKDAY_ABBR(d)       (get_weekday_name_abbr (g_date_time_get_day_of_week (datetime)))
00158 #define WEEKDAY_FULL(d)       (get_weekday_name (g_date_time_get_day_of_week (datetime)))
00159 
00160 #define MONTH_ABBR(d)         (get_month_name_abbr (g_date_time_get_month (datetime)))
00161 #define MONTH_FULL(d)         (get_month_name (g_date_time_get_month (datetime)))
00162 
00163 /* Translators: this is the preferred format for expressing the date */
00164 #define GET_PREFERRED_DATE(d) (g_date_time_format ((d), C_("GDateTime", "%m/%d/%y")))
00165 
00166 /* Translators: this is the preferred format for expressing the time */
00167 #define GET_PREFERRED_TIME(d) (g_date_time_format ((d), C_("GDateTime", "%H:%M:%S")))
00168 
00169 #define SECS_PER_MINUTE (60)
00170 #define SECS_PER_HOUR   (60 * SECS_PER_MINUTE)
00171 #define SECS_PER_DAY    (24 * SECS_PER_HOUR)
00172 #define SECS_PER_YEAR   (365 * SECS_PER_DAY)
00173 #define SECS_PER_JULIAN (DAYS_PER_PERIOD * SECS_PER_DAY)
00174 
00175 static const guint16 days_in_months[2][13] =
00176 {
00177   { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
00178   { 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
00179 };
00180 
00181 static const guint16 days_in_year[2][13] =
00182 {
00183   {  0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
00184   {  0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
00185 };
00186 
00187 static const gchar *
00188 get_month_name (gint month)
00189 {
00190   switch (month)
00191     {
00192     case 1:
00193       return C_("full month name", "January");
00194     case 2:
00195       return C_("full month name", "February");
00196     case 3:
00197       return C_("full month name", "March");
00198     case 4:
00199       return C_("full month name", "April");
00200     case 5:
00201       return C_("full month name", "May");
00202     case 6:
00203       return C_("full month name", "June");
00204     case 7:
00205       return C_("full month name", "July");
00206     case 8:
00207       return C_("full month name", "August");
00208     case 9:
00209       return C_("full month name", "September");
00210     case 10:
00211       return C_("full month name", "October");
00212     case 11:
00213       return C_("full month name", "November");
00214     case 12:
00215       return C_("full month name", "December");
00216 
00217     default:
00218       g_warning ("Invalid month number %d", month);
00219     }
00220 
00221   return NULL;
00222 }
00223 
00224 static const gchar *
00225 get_month_name_abbr (gint month)
00226 {
00227   switch (month)
00228     {
00229     case 1:
00230       return C_("abbreviated month name", "Jan");
00231     case 2:
00232       return C_("abbreviated month name", "Feb");
00233     case 3:
00234       return C_("abbreviated month name", "Mar");
00235     case 4:
00236       return C_("abbreviated month name", "Apr");
00237     case 5:
00238       return C_("abbreviated month name", "May");
00239     case 6:
00240       return C_("abbreviated month name", "Jun");
00241     case 7:
00242       return C_("abbreviated month name", "Jul");
00243     case 8:
00244       return C_("abbreviated month name", "Aug");
00245     case 9:
00246       return C_("abbreviated month name", "Sep");
00247     case 10:
00248       return C_("abbreviated month name", "Oct");
00249     case 11:
00250       return C_("abbreviated month name", "Nov");
00251     case 12:
00252       return C_("abbreviated month name", "Dec");
00253 
00254     default:
00255       g_warning ("Invalid month number %d", month);
00256     }
00257 
00258   return NULL;
00259 }
00260 
00261 static const gchar *
00262 get_weekday_name (gint day)
00263 {
00264   switch (day)
00265     {
00266     case 1:
00267       return C_("full weekday name", "Monday");
00268     case 2:
00269       return C_("full weekday name", "Tuesday");
00270     case 3:
00271       return C_("full weekday name", "Wednesday");
00272     case 4:
00273       return C_("full weekday name", "Thursday");
00274     case 5:
00275       return C_("full weekday name", "Friday");
00276     case 6:
00277       return C_("full weekday name", "Saturday");
00278     case 7:
00279       return C_("full weekday name", "Sunday");
00280 
00281     default:
00282       g_warning ("Invalid week day number %d", day);
00283     }
00284 
00285   return NULL;
00286 }
00287 
00288 static const gchar *
00289 get_weekday_name_abbr (gint day)
00290 {
00291   switch (day)
00292     {
00293     case 1:
00294       return C_("abbreviated weekday name", "Mon");
00295     case 2:
00296       return C_("abbreviated weekday name", "Tue");
00297     case 3:
00298       return C_("abbreviated weekday name", "Wed");
00299     case 4:
00300       return C_("abbreviated weekday name", "Thu");
00301     case 5:
00302       return C_("abbreviated weekday name", "Fri");
00303     case 6:
00304       return C_("abbreviated weekday name", "Sat");
00305     case 7:
00306       return C_("abbreviated weekday name", "Sun");
00307 
00308     default:
00309       g_warning ("Invalid week day number %d", day);
00310     }
00311 
00312   return NULL;
00313 }
00314 
00315 static inline gint
00316 ymd_to_days (gint year,
00317              gint month,
00318              gint day)
00319 {
00320   gint64 days;
00321 
00322   days = (year - 1) * 365 + ((year - 1) / 4) - ((year - 1) / 100)
00323       + ((year - 1) / 400);
00324 
00325   days += days_in_year[0][month - 1];
00326   if (GREGORIAN_LEAP (year) && month > 2)
00327     day++;
00328 
00329   days += day;
00330 
00331   return days;
00332 }
00333 
00334 static void
00335 g_date_time_get_week_number (GDateTime *datetime,
00336                              gint      *week_number,
00337                              gint      *day_of_week,
00338                              gint      *day_of_year)
00339 {
00340   gint a, b, c, d, e, f, g, n, s, month, day, year;
00341 
00342   g_date_time_get_ymd (datetime, &year, &month, &day);
00343 
00344   if (month <= 2)
00345     {
00346       a = g_date_time_get_year (datetime) - 1;
00347       b = (a / 4) - (a / 100) + (a / 400);
00348       c = ((a - 1) / 4) - ((a - 1) / 100) + ((a - 1) / 400);
00349       s = b - c;
00350       e = 0;
00351       f = day - 1 + (31 * (month - 1));
00352     }
00353   else
00354     {
00355       a = year;
00356       b = (a / 4) - (a / 100) + (a / 400);
00357       c = ((a - 1) / 4) - ((a - 1) / 100) + ((a - 1) / 400);
00358       s = b - c;
00359       e = s + 1;
00360       f = day + (((153 * (month - 3)) + 2) / 5) + 58 + s;
00361     }
00362 
00363   g = (a + b) % 7;
00364   d = (f + g - e) % 7;
00365   n = f + 3 - d;
00366 
00367   if (week_number)
00368     {
00369       if (n < 0)
00370         *week_number = 53 - ((g - s) / 5);
00371       else if (n > 364 + s)
00372         *week_number = 1;
00373       else
00374         *week_number = (n / 7) + 1;
00375     }
00376 
00377   if (day_of_week)
00378     *day_of_week = d + 1;
00379 
00380   if (day_of_year)
00381     *day_of_year = f + 1;
00382 }
00383 
00384 /* Lifecycle {{{1 */
00385 
00386 static GDateTime *
00387 g_date_time_alloc (GTimeZone *tz)
00388 {
00389   GDateTime *datetime;
00390 
00391   datetime = g_slice_new0 (GDateTime);
00392   datetime->tz = g_time_zone_ref (tz);
00393   datetime->ref_count = 1;
00394 
00395   return datetime;
00396 }
00397 
00408 GDateTime *
00409 g_date_time_ref (GDateTime *datetime)
00410 {
00411   g_return_val_if_fail (datetime != NULL, NULL);
00412   g_return_val_if_fail (datetime->ref_count > 0, NULL);
00413 
00414   g_atomic_int_inc (&datetime->ref_count);
00415 
00416   return datetime;
00417 }
00418 
00430 void
00431 g_date_time_unref (GDateTime *datetime)
00432 {
00433   g_return_if_fail (datetime != NULL);
00434   g_return_if_fail (datetime->ref_count > 0);
00435 
00436   if (g_atomic_int_dec_and_test (&datetime->ref_count))
00437     {
00438       g_time_zone_unref (datetime->tz);
00439       g_slice_free (GDateTime, datetime);
00440     }
00441 }
00442 
00443 /* Internal state transformers {{{1 */
00444 /*< internal >
00445  * g_date_time_to_instant:
00446  * @datetime: a #GDateTime
00447  *
00448  * Convert a @datetime into an instant.
00449  *
00450  * An instant is a number that uniquely describes a particular
00451  * microsecond in time, taking time zone considerations into account.
00452  * (ie: "03:00 -0400" is the same instant as "02:00 -0500").
00453  *
00454  * An instant is always positive but we use a signed return value to
00455  * avoid troubles with C.
00456  */
00457 static gint64
00458 g_date_time_to_instant (GDateTime *datetime)
00459 {
00460   gint64 offset;
00461 
00462   offset = g_time_zone_get_offset (datetime->tz, datetime->interval);
00463   offset *= USEC_PER_SECOND;
00464 
00465   return datetime->days * USEC_PER_DAY + datetime->usec - offset;
00466 }
00467 
00468 /*< internal >
00469  * g_date_time_from_instant:
00470  * @tz: a #GTimeZone
00471  * @instant: a instant in time
00472  *
00473  * Creates a #GDateTime from a time zone and an instant.
00474  *
00475  * This might fail if the time ends up being out of range.
00476  */
00477 static GDateTime *
00478 g_date_time_from_instant (GTimeZone *tz,
00479                           gint64     instant)
00480 {
00481   GDateTime *datetime;
00482   gint64 offset;
00483 
00484   if (instant < 0 || instant > 1000000000000000000)
00485     return NULL;
00486 
00487   datetime = g_date_time_alloc (tz);
00488   datetime->interval = g_time_zone_find_interval (tz,
00489                                                   G_TIME_TYPE_UNIVERSAL,
00490                                                   INSTANT_TO_UNIX (instant));
00491   offset = g_time_zone_get_offset (datetime->tz, datetime->interval);
00492   offset *= USEC_PER_SECOND;
00493 
00494   instant += offset;
00495 
00496   datetime->days = instant / USEC_PER_DAY;
00497   datetime->usec = instant % USEC_PER_DAY;
00498 
00499   if (datetime->days < 1 || 3652059 < datetime->days)
00500     {
00501       g_date_time_unref (datetime);
00502       datetime = NULL;
00503     }
00504 
00505   return datetime;
00506 }
00507 
00508 
00509 /*< internal >
00510  * g_date_time_deal_with_date_change:
00511  * @datetime: a #GDateTime
00512  *
00513  * This function should be called whenever the date changes by adding
00514  * days, months or years.  It does three things.
00515  *
00516  * First, we ensure that the date falls between 0001-01-01 and
00517  * 9999-12-31 and return %FALSE if it does not.
00518  *
00519  * Next we update the ->interval field.
00520  *
00521  * Finally, we ensure that the resulting date and time pair exists (by
00522  * ensuring that our time zone has an interval containing it) and
00523  * adjusting as required.  For example, if we have the time 02:30:00 on
00524  * March 13 2010 in Toronto and we add 1 day to it, we would end up with
00525  * 2:30am on March 14th, which doesn't exist.  In that case, we bump the
00526  * time up to 3:00am.
00527  */
00528 static gboolean
00529 g_date_time_deal_with_date_change (GDateTime *datetime)
00530 {
00531   GTimeType was_dst;
00532   gint64 full_time;
00533   gint64 usec;
00534 
00535   if (datetime->days < 1 || datetime->days > 3652059)
00536     return FALSE;
00537 
00538   was_dst = g_time_zone_is_dst (datetime->tz, datetime->interval);
00539 
00540   full_time = datetime->days * USEC_PER_DAY + datetime->usec;
00541 
00542 
00543   usec = full_time % USEC_PER_SECOND;
00544   full_time /= USEC_PER_SECOND;
00545   full_time -= UNIX_EPOCH_START * SEC_PER_DAY;
00546 
00547   datetime->interval = g_time_zone_adjust_time (datetime->tz,
00548                                                 was_dst,
00549                                                 &full_time);
00550   full_time += UNIX_EPOCH_START * SEC_PER_DAY;
00551   full_time *= USEC_PER_SECOND;
00552   full_time += usec;
00553 
00554   datetime->days = full_time / USEC_PER_DAY;
00555   datetime->usec = full_time % USEC_PER_DAY;
00556 
00557   /* maybe daylight time caused us to shift to a different day,
00558    * but it definitely didn't push us into a different year */
00559   return TRUE;
00560 }
00561 
00562 static GDateTime *
00563 g_date_time_replace_days (GDateTime *datetime,
00564                           gint       days)
00565 {
00566   GDateTime *new;
00567 
00568   new = g_date_time_alloc (datetime->tz);
00569   new->interval = datetime->interval;
00570   new->usec = datetime->usec;
00571   new->days = days;
00572 
00573   if (!g_date_time_deal_with_date_change (new))
00574     {
00575       g_date_time_unref (new);
00576       new = NULL;
00577     }
00578 
00579   return new;
00580 }
00581 
00582 /* now/unix/timeval Constructors {{{1 */
00583 
00584 /*< internal >
00585  * g_date_time_new_from_timeval:
00586  * @tz: a #GTimeZone
00587  * @tv: a #GTimeVal
00588  *
00589  * Creates a #GDateTime corresponding to the given #GTimeVal @tv in the
00590  * given time zone @tz.
00591  *
00592  * The time contained in a #GTimeVal is always stored in the form of
00593  * seconds elapsed since 1970-01-01 00:00:00 UTC, regardless of the
00594  * given time zone.
00595  *
00596  * This call can fail (returning %NULL) if @tv represents a time outside
00597  * of the supported range of #GDateTime.
00598  *
00599  * You should release the return value by calling g_date_time_unref()
00600  * when you are done with it.
00601  *
00602  * Returns: a new #GDateTime, or %NULL
00603  *
00604  * Since: 2.26
00605  **/
00606 static GDateTime *
00607 g_date_time_new_from_timeval (GTimeZone      *tz,
00608                               const GTimeVal *tv)
00609 {
00610   return g_date_time_from_instant (tz, tv->tv_usec +
00611                                    UNIX_TO_INSTANT (tv->tv_sec));
00612 }
00613 
00614 /*< internal >
00615  * g_date_time_new_from_unix:
00616  * @tz: a #GTimeZone
00617  * @t: the Unix time
00618  *
00619  * Creates a #GDateTime corresponding to the given Unix time @t in the
00620  * given time zone @tz.
00621  *
00622  * Unix time is the number of seconds that have elapsed since 1970-01-01
00623  * 00:00:00 UTC, regardless of the time zone given.
00624  *
00625  * This call can fail (returning %NULL) if @t represents a time outside
00626  * of the supported range of #GDateTime.
00627  *
00628  * You should release the return value by calling g_date_time_unref()
00629  * when you are done with it.
00630  *
00631  * Returns: a new #GDateTime, or %NULL
00632  *
00633  * Since: 2.26
00634  **/
00635 static GDateTime *
00636 g_date_time_new_from_unix (GTimeZone *tz,
00637                            gint64     secs)
00638 {
00639   return g_date_time_from_instant (tz, UNIX_TO_INSTANT (secs));
00640 }
00641 
00661 GDateTime *
00662 g_date_time_new_now (GTimeZone *tz)
00663 {
00664   GTimeVal tv;
00665 
00666   g_get_current_time (&tv);
00667 
00668   return g_date_time_new_from_timeval (tz, &tv);
00669 }
00670 
00684 GDateTime *
00685 g_date_time_new_now_local (void)
00686 {
00687   GDateTime *datetime;
00688   GTimeZone *local;
00689 
00690   local = g_time_zone_new_local ();
00691   datetime = g_date_time_new_now (local);
00692   g_time_zone_unref (local);
00693 
00694   return datetime;
00695 }
00696 
00709 GDateTime *
00710 g_date_time_new_now_utc (void)
00711 {
00712   GDateTime *datetime;
00713   GTimeZone *utc;
00714 
00715   utc = g_time_zone_new_utc ();
00716   datetime = g_date_time_new_now (utc);
00717   g_time_zone_unref (utc);
00718 
00719   return datetime;
00720 }
00721 
00742 GDateTime *
00743 g_date_time_new_from_unix_local (gint64 t)
00744 {
00745   GDateTime *datetime;
00746   GTimeZone *local;
00747 
00748   local = g_time_zone_new_local ();
00749   datetime = g_date_time_new_from_unix (local, t);
00750   g_time_zone_unref (local);
00751 
00752   return datetime;
00753 }
00754 
00774 GDateTime *
00775 g_date_time_new_from_unix_utc (gint64 t)
00776 {
00777   GDateTime *datetime;
00778   GTimeZone *utc;
00779 
00780   utc = g_time_zone_new_utc ();
00781   datetime = g_date_time_new_from_unix (utc, t);
00782   g_time_zone_unref (utc);
00783 
00784   return datetime;
00785 }
00786 
00808 GDateTime *
00809 g_date_time_new_from_timeval_local (const GTimeVal *tv)
00810 {
00811   GDateTime *datetime;
00812   GTimeZone *local;
00813 
00814   local = g_time_zone_new_local ();
00815   datetime = g_date_time_new_from_timeval (local, tv);
00816   g_time_zone_unref (local);
00817 
00818   return datetime;
00819 }
00820 
00840 GDateTime *
00841 g_date_time_new_from_timeval_utc (const GTimeVal *tv)
00842 {
00843   GDateTime *datetime;
00844   GTimeZone *utc;
00845 
00846   utc = g_time_zone_new_utc ();
00847   datetime = g_date_time_new_from_timeval (utc, tv);
00848   g_time_zone_unref (utc);
00849 
00850   return datetime;
00851 }
00852 
00853 /* full new functions {{{1 */
00854 
00898 GDateTime *
00899 g_date_time_new (GTimeZone *tz,
00900                  gint       year,
00901                  gint       month,
00902                  gint       day,
00903                  gint       hour,
00904                  gint       minute,
00905                  gdouble    seconds)
00906 {
00907   GDateTime *datetime;
00908   gint64 full_time;
00909 
00910   datetime = g_date_time_alloc (tz);
00911   datetime->days = ymd_to_days (year, month, day);
00912   datetime->usec = (hour   * USEC_PER_HOUR)
00913                  + (minute * USEC_PER_MINUTE)
00914                  + (gint64) (seconds * USEC_PER_SECOND);
00915 
00916   full_time = SEC_PER_DAY *
00917                 (ymd_to_days (year, month, day) - UNIX_EPOCH_START) +
00918               SECS_PER_HOUR * hour +
00919               SECS_PER_MINUTE * minute +
00920               (int) seconds;
00921 
00922   datetime->interval = g_time_zone_adjust_time (datetime->tz,
00923                                                 G_TIME_TYPE_STANDARD,
00924                                                 &full_time);
00925 
00926   full_time += UNIX_EPOCH_START * SEC_PER_DAY;
00927   datetime->days = full_time / SEC_PER_DAY;
00928   datetime->usec = (full_time % SEC_PER_DAY) * USEC_PER_SECOND;
00929   datetime->usec += ((int) (seconds * USEC_PER_SECOND)) % USEC_PER_SECOND;
00930 
00931   return datetime;
00932 }
00933 
00953 GDateTime *
00954 g_date_time_new_local (gint    year,
00955                        gint    month,
00956                        gint    day,
00957                        gint    hour,
00958                        gint    minute,
00959                        gdouble seconds)
00960 {
00961   GDateTime *datetime;
00962   GTimeZone *local;
00963 
00964   local = g_time_zone_new_local ();
00965   datetime = g_date_time_new (local, year, month, day, hour, minute, seconds);
00966   g_time_zone_unref (local);
00967 
00968   return datetime;
00969 }
00970 
00990 GDateTime *
00991 g_date_time_new_utc (gint    year,
00992                      gint    month,
00993                      gint    day,
00994                      gint    hour,
00995                      gint    minute,
00996                      gdouble seconds)
00997 {
00998   GDateTime *datetime;
00999   GTimeZone *utc;
01000 
01001   utc = g_time_zone_new_utc ();
01002   datetime = g_date_time_new (utc, year, month, day, hour, minute, seconds);
01003   g_time_zone_unref (utc);
01004 
01005   return datetime;
01006 }
01007 
01008 /* Adders {{{1 */
01009 
01022 GDateTime*
01023 g_date_time_add (GDateTime *datetime,
01024                  GTimeSpan  timespan)
01025 {
01026   return g_date_time_from_instant (datetime->tz, timespan +
01027                                    g_date_time_to_instant (datetime));
01028 }
01029 
01043 GDateTime *
01044 g_date_time_add_years (GDateTime *datetime,
01045                        gint       years)
01046 {
01047   gint year, month, day;
01048 
01049   g_return_val_if_fail (datetime != NULL, NULL);
01050 
01051   if (years < -10000 || years > 10000)
01052     return NULL;
01053 
01054   g_date_time_get_ymd (datetime, &year, &month, &day);
01055   year += years;
01056 
01057   /* only possible issue is if we've entered a year with no February 29
01058    */
01059   if (month == 2 && day == 29 && !GREGORIAN_LEAP (year))
01060     day = 28;
01061 
01062   return g_date_time_replace_days (datetime, ymd_to_days (year, month, day));
01063 }
01064 
01078 GDateTime*
01079 g_date_time_add_months (GDateTime *datetime,
01080                         gint       months)
01081 {
01082   gint year, month, day;
01083 
01084   g_return_val_if_fail (datetime != NULL, NULL);
01085   g_date_time_get_ymd (datetime, &year, &month, &day);
01086 
01087   if (months < -120000 || months > 120000)
01088     return NULL;
01089 
01090   year += months / 12;
01091   month += months % 12;
01092   if (month < 1)
01093     {
01094       month += 12;
01095       year--;
01096     }
01097   else if (month > 12)
01098     {
01099       month -= 12;
01100       year++;
01101     }
01102 
01103   day = MIN (day, days_in_months[GREGORIAN_LEAP (year)][month]);
01104 
01105   return g_date_time_replace_days (datetime, ymd_to_days (year, month, day));
01106 }
01107 
01121 GDateTime*
01122 g_date_time_add_weeks (GDateTime *datetime,
01123                        gint             weeks)
01124 {
01125   g_return_val_if_fail (datetime != NULL, NULL);
01126 
01127   return g_date_time_add_days (datetime, weeks * 7);
01128 }
01129 
01143 GDateTime*
01144 g_date_time_add_days (GDateTime *datetime,
01145                       gint       days)
01146 {
01147   g_return_val_if_fail (datetime != NULL, NULL);
01148 
01149   if (days < -3660000 || days > 3660000)
01150     return NULL;
01151 
01152   return g_date_time_replace_days (datetime, datetime->days + days);
01153 }
01154 
01167 GDateTime*
01168 g_date_time_add_hours (GDateTime *datetime,
01169                        gint       hours)
01170 {
01171   return g_date_time_add (datetime, hours * USEC_PER_HOUR);
01172 }
01173 
01186 GDateTime*
01187 g_date_time_add_minutes (GDateTime *datetime,
01188                          gint             minutes)
01189 {
01190   return g_date_time_add (datetime, minutes * USEC_PER_MINUTE);
01191 }
01192 
01193 
01206 GDateTime*
01207 g_date_time_add_seconds (GDateTime *datetime,
01208                          gdouble    seconds)
01209 {
01210   return g_date_time_add (datetime, seconds * USEC_PER_SECOND);
01211 }
01212 
01231 GDateTime *
01232 g_date_time_add_full (GDateTime *datetime,
01233                       gint       years,
01234                       gint       months,
01235                       gint       days,
01236                       gint       hours,
01237                       gint       minutes,
01238                       gdouble    seconds)
01239 {
01240   gint year, month, day;
01241   gint64 full_time;
01242   GDateTime *new;
01243   gint interval;
01244 
01245   g_return_val_if_fail (datetime != NULL, NULL);
01246   g_date_time_get_ymd (datetime, &year, &month, &day);
01247 
01248   months += years * 12;
01249 
01250   if (months < -120000 || months > 120000)
01251     return NULL;
01252 
01253   if (days < -3660000 || days > 3660000)
01254     return NULL;
01255 
01256   year += months / 12;
01257   month += months % 12;
01258   if (month < 1)
01259     {
01260       month += 12;
01261       year--;
01262     }
01263   else if (month > 12)
01264     {
01265       month -= 12;
01266       year++;
01267     }
01268 
01269   day = MIN (day, days_in_months[GREGORIAN_LEAP (year)][month]);
01270 
01271   /* full_time is now in unix (local) time */
01272   full_time = datetime->usec / USEC_PER_SECOND + SEC_PER_DAY *
01273     (ymd_to_days (year, month, day) + days - UNIX_EPOCH_START);
01274 
01275   interval = g_time_zone_adjust_time (datetime->tz,
01276                                       g_time_zone_is_dst (datetime->tz,
01277                                                           datetime->interval),
01278                                       &full_time);
01279 
01280   /* move to UTC unix time */
01281   full_time -= g_time_zone_get_offset (datetime->tz, interval);
01282 
01283   /* convert back to an instant, add back fractional seconds */
01284   full_time += UNIX_EPOCH_START * SEC_PER_DAY;
01285   full_time = full_time * USEC_PER_SECOND +
01286               datetime->usec % USEC_PER_SECOND;
01287 
01288   /* do the actual addition now */
01289   full_time += (hours * USEC_PER_HOUR) +
01290                (minutes * USEC_PER_MINUTE) +
01291                (gint64) (seconds * USEC_PER_SECOND);
01292 
01293   /* find the new interval */
01294   interval = g_time_zone_find_interval (datetime->tz,
01295                                         G_TIME_TYPE_UNIVERSAL,
01296                                         INSTANT_TO_UNIX (full_time));
01297 
01298   /* convert back into local time */
01299   full_time += USEC_PER_SECOND *
01300                g_time_zone_get_offset (datetime->tz, interval);
01301 
01302   /* split into days and usec of a new datetime */
01303   new = g_date_time_alloc (datetime->tz);
01304   new->interval = interval;
01305   new->days = full_time / USEC_PER_DAY;
01306   new->usec = full_time % USEC_PER_DAY;
01307 
01308   /* XXX validate */
01309 
01310   return new;
01311 }
01312 
01313 /* Compare, difference, hash, equal {{{1 */
01327 gint
01328 g_date_time_compare (gconstpointer dt1,
01329                      gconstpointer dt2)
01330 {
01331   gint64 difference;
01332 
01333   difference = g_date_time_difference ((GDateTime *) dt1, (GDateTime *) dt2);
01334 
01335   if (difference < 0)
01336     return -1;
01337 
01338   else if (difference > 0)
01339     return 1;
01340 
01341   else
01342     return 0;
01343 }
01344 
01359 GTimeSpan
01360 g_date_time_difference (GDateTime *end,
01361                         GDateTime *begin)
01362 {
01363   g_return_val_if_fail (begin != NULL, 0);
01364   g_return_val_if_fail (end != NULL, 0);
01365 
01366   return g_date_time_to_instant (end) -
01367          g_date_time_to_instant (begin);
01368 }
01369 
01380 guint
01381 g_date_time_hash (gconstpointer datetime)
01382 {
01383   return g_date_time_to_instant ((GDateTime *) datetime);
01384 }
01385 
01400 gboolean
01401 g_date_time_equal (gconstpointer dt1,
01402                    gconstpointer dt2)
01403 {
01404   return g_date_time_difference ((GDateTime *) dt1, (GDateTime *) dt2) == 0;
01405 }
01406 
01407 /* Year, Month, Day Getters {{{1 */
01419 void
01420 g_date_time_get_ymd (GDateTime *datetime,
01421                      gint      *year,
01422                      gint      *month,
01423                      gint      *day)
01424 {
01425   gint the_year;
01426   gint the_month;
01427   gint the_day;
01428   gint remaining_days;
01429   gint y100_cycles;
01430   gint y4_cycles;
01431   gint y1_cycles;
01432   gint preceding;
01433   gboolean leap;
01434 
01435   g_return_if_fail (datetime != NULL);
01436 
01437   remaining_days = datetime->days;
01438 
01439   /*
01440    * We need to convert an offset in days to its year/month/day representation.
01441    * Leap years makes this a little trickier than it should be, so we use
01442    * 400, 100 and 4 years cycles here to get to the correct year.
01443    */
01444 
01445   /* Our days offset starts sets 0001-01-01 as day 1, if it was day 0 our
01446    * math would be simpler, so let's do it */
01447   remaining_days--;
01448 
01449   the_year = (remaining_days / DAYS_IN_400YEARS) * 400 + 1;
01450   remaining_days = remaining_days % DAYS_IN_400YEARS;
01451 
01452   y100_cycles = remaining_days / DAYS_IN_100YEARS;
01453   remaining_days = remaining_days % DAYS_IN_100YEARS;
01454   the_year += y100_cycles * 100;
01455 
01456   y4_cycles = remaining_days / DAYS_IN_4YEARS;
01457   remaining_days = remaining_days % DAYS_IN_4YEARS;
01458   the_year += y4_cycles * 4;
01459 
01460   y1_cycles = remaining_days / 365;
01461   the_year += y1_cycles;
01462   remaining_days = remaining_days % 365;
01463 
01464   if (y1_cycles == 4 || y100_cycles == 4) {
01465     g_assert (remaining_days == 0);
01466 
01467     /* special case that indicates that the date is actually one year before,
01468      * in the 31th of December */
01469     the_year--;
01470     the_month = 12;
01471     the_day = 31;
01472     goto end;
01473   }
01474 
01475   /* now get the month and the day */
01476   leap = y1_cycles == 3 && (y4_cycles != 24 || y100_cycles == 3);
01477 
01478   g_assert (leap == GREGORIAN_LEAP(the_year));
01479 
01480   the_month = (remaining_days + 50) >> 5;
01481   preceding = (days_in_year[0][the_month - 1] + (the_month > 2 && leap));
01482   if (preceding > remaining_days)
01483     {
01484       /* estimate is too large */
01485       the_month -= 1;
01486       preceding -= leap ? days_in_months[1][the_month]
01487                         : days_in_months[0][the_month];
01488     }
01489 
01490   remaining_days -= preceding;
01491   g_assert(0 <= remaining_days);
01492 
01493   the_day = remaining_days + 1;
01494 
01495 end:
01496   if (year)
01497     *year = the_year;
01498   if (month)
01499     *month = the_month;
01500   if (day)
01501     *day = the_day;
01502 }
01503 
01514 gint
01515 g_date_time_get_year (GDateTime *datetime)
01516 {
01517   gint year;
01518 
01519   g_return_val_if_fail (datetime != NULL, 0);
01520 
01521   g_date_time_get_ymd (datetime, &year, NULL, NULL);
01522 
01523   return year;
01524 }
01525 
01537 gint
01538 g_date_time_get_month (GDateTime *datetime)
01539 {
01540   gint month;
01541 
01542   g_return_val_if_fail (datetime != NULL, 0);
01543 
01544   g_date_time_get_ymd (datetime, NULL, &month, NULL);
01545 
01546   return month;
01547 }
01548 
01560 gint
01561 g_date_time_get_day_of_month (GDateTime *datetime)
01562 {
01563   gint           day_of_year,
01564                  i;
01565   const guint16 *days;
01566   guint16        last = 0;
01567 
01568   g_return_val_if_fail (datetime != NULL, 0);
01569 
01570   days = days_in_year[GREGORIAN_LEAP (g_date_time_get_year (datetime)) ? 1 : 0];
01571   g_date_time_get_week_number (datetime, NULL, NULL, &day_of_year);
01572 
01573   for (i = 1; i <= 12; i++)
01574     {
01575       if (days [i] >= day_of_year)
01576         return day_of_year - last;
01577       last = days [i];
01578     }
01579 
01580   g_warn_if_reached ();
01581   return 0;
01582 }
01583 
01584 /* Week of year / day of week getters {{{1 */
01595 gint
01596 g_date_time_get_week_of_year (GDateTime *datetime)
01597 {
01598   gint weeknum;
01599 
01600   g_return_val_if_fail (datetime != NULL, 0);
01601 
01602   g_date_time_get_week_number (datetime, &weeknum, NULL, NULL);
01603 
01604   return weeknum;
01605 }
01606 
01618 gint
01619 g_date_time_get_day_of_week (GDateTime *datetime)
01620 {
01621   gint a, y, m,
01622        year  = 0,
01623        month = 0,
01624        day   = 0,
01625        dow;
01626 
01627   g_return_val_if_fail (datetime != NULL, 0);
01628 
01629   /*
01630    * See Calendar FAQ Section 2.6 for algorithm information
01631    * http://www.tondering.dk/claus/cal/calendar29.txt
01632    */
01633 
01634   g_date_time_get_ymd (datetime, &year, &month, &day);
01635   a = (14 - month) / 12;
01636   y = year - a;
01637   m = month + (12 * a) - 2;
01638   dow = ((day + y + (y / 4) - (y / 100) + (y / 400) + (31 * m) / 12) % 7);
01639 
01640   /* 1 is Monday and 7 is Sunday */
01641   return (dow == 0) ? 7 : dow;
01642 }
01643 
01644 /* Day of year getter {{{1 */
01656 gint
01657 g_date_time_get_day_of_year (GDateTime *datetime)
01658 {
01659   gint doy = 0;
01660 
01661   g_return_val_if_fail (datetime != NULL, 0);
01662 
01663   g_date_time_get_week_number (datetime, NULL, NULL, &doy);
01664   return doy;
01665 }
01666 
01667 /* Time component getters {{{1 */
01668 
01679 gint
01680 g_date_time_get_hour (GDateTime *datetime)
01681 {
01682   g_return_val_if_fail (datetime != NULL, 0);
01683 
01684   return (datetime->usec / USEC_PER_HOUR);
01685 }
01686 
01697 gint
01698 g_date_time_get_minute (GDateTime *datetime)
01699 {
01700   g_return_val_if_fail (datetime != NULL, 0);
01701 
01702   return (datetime->usec % USEC_PER_HOUR) / USEC_PER_MINUTE;
01703 }
01704 
01715 gint
01716 g_date_time_get_second (GDateTime *datetime)
01717 {
01718   g_return_val_if_fail (datetime != NULL, 0);
01719 
01720   return (datetime->usec % USEC_PER_MINUTE) / USEC_PER_SECOND;
01721 }
01722 
01733 gint
01734 g_date_time_get_microsecond (GDateTime *datetime)
01735 {
01736   g_return_val_if_fail (datetime != NULL, 0);
01737 
01738   return (datetime->usec % USEC_PER_SECOND);
01739 }
01740 
01752 gdouble
01753 g_date_time_get_seconds (GDateTime *datetime)
01754 {
01755   g_return_val_if_fail (datetime != NULL, 0);
01756 
01757   return (datetime->usec % USEC_PER_MINUTE) / 1000000.0;
01758 }
01759 
01760 /* Exporters {{{1 */
01775 gint64
01776 g_date_time_to_unix (GDateTime *datetime)
01777 {
01778   return INSTANT_TO_UNIX (g_date_time_to_instant (datetime));
01779 }
01780 
01804 gboolean
01805 g_date_time_to_timeval (GDateTime *datetime,
01806                         GTimeVal  *tv)
01807 {
01808   tv->tv_sec = INSTANT_TO_UNIX (g_date_time_to_instant (datetime));
01809   tv->tv_usec = datetime->usec % USEC_PER_SECOND;
01810 
01811   return TRUE;
01812 }
01813 
01814 /* Timezone queries {{{1 */
01833 GTimeSpan
01834 g_date_time_get_utc_offset (GDateTime *datetime)
01835 {
01836   gint offset;
01837 
01838   g_return_val_if_fail (datetime != NULL, 0);
01839 
01840   offset = g_time_zone_get_offset (datetime->tz, datetime->interval);
01841 
01842   return (gint64) offset * USEC_PER_SECOND;
01843 }
01844 
01862 const gchar *
01863 g_date_time_get_timezone_abbreviation (GDateTime *datetime)
01864 {
01865   g_return_val_if_fail (datetime != NULL, NULL);
01866 
01867   return g_time_zone_get_abbreviation (datetime->tz, datetime->interval);
01868 }
01869 
01881 gboolean
01882 g_date_time_is_daylight_savings (GDateTime *datetime)
01883 {
01884   g_return_val_if_fail (datetime != NULL, FALSE);
01885 
01886   return g_time_zone_is_dst (datetime->tz, datetime->interval);
01887 }
01888 
01889 /* Timezone convert {{{1 */
01909 GDateTime *
01910 g_date_time_to_timezone (GDateTime *datetime,
01911                          GTimeZone *tz)
01912 {
01913   return g_date_time_from_instant (tz, g_date_time_to_instant (datetime));
01914 }
01915 
01930 GDateTime *
01931 g_date_time_to_local (GDateTime *datetime)
01932 {
01933   GDateTime *new;
01934   GTimeZone *local;
01935 
01936   local = g_time_zone_new_local ();
01937   new = g_date_time_to_timezone (datetime, local);
01938   g_time_zone_unref (local);
01939 
01940   return new;
01941 }
01942 
01957 GDateTime *
01958 g_date_time_to_utc (GDateTime *datetime)
01959 {
01960   GDateTime *new;
01961   GTimeZone *utc;
01962 
01963   utc = g_time_zone_new_utc ();
01964   new = g_date_time_to_timezone (datetime, utc);
01965   g_time_zone_unref (utc);
01966 
01967   return new;
01968 }
01969 
01970 /* Format {{{1 */
02161 gchar *
02162 g_date_time_format (GDateTime *datetime,
02163                     const gchar     *format)
02164 {
02165   GString  *outstr;
02166   gchar    *tmp;
02167   gunichar  c;
02168   glong     utf8len;
02169   gboolean  in_mod;
02170 
02171   g_return_val_if_fail (datetime != NULL, NULL);
02172   g_return_val_if_fail (format != NULL, NULL);
02173   g_return_val_if_fail (g_utf8_validate (format, -1, NULL), NULL);
02174 
02175   outstr = g_string_sized_new (strlen (format) * 2);
02176   utf8len = g_utf8_strlen (format, -1);
02177   in_mod = FALSE;
02178 
02179   for (; *format; format = g_utf8_next_char(format))
02180     {
02181       c = g_utf8_get_char (format);
02182 
02183       switch (c)
02184         {
02185         case '%':
02186           if (!in_mod)
02187             {
02188               in_mod = TRUE;
02189               break;
02190             }
02191             /* Fall through */
02192         default:
02193           if (in_mod)
02194             {
02195               switch (c)
02196                 {
02197                 case 'a':
02198                   g_string_append (outstr, WEEKDAY_ABBR (datetime));
02199                   break;
02200                 case 'A':
02201                   g_string_append (outstr, WEEKDAY_FULL (datetime));
02202                   break;
02203                 case 'b':
02204                   g_string_append (outstr, MONTH_ABBR (datetime));
02205                   break;
02206                 case 'B':
02207                   g_string_append (outstr, MONTH_FULL (datetime));
02208                   break;
02209                 case 'd':
02210                   g_string_append_printf (outstr, "%02d", g_date_time_get_day_of_month (datetime));
02211                   break;
02212                 case 'e':
02213                   g_string_append_printf (outstr, "%2d", g_date_time_get_day_of_month (datetime));
02214                   break;
02215                 case 'F':
02216                   g_string_append_printf (outstr, "%d-%02d-%02d",
02217                                           g_date_time_get_year (datetime),
02218                                           g_date_time_get_month (datetime),
02219                                           g_date_time_get_day_of_month (datetime));
02220                   break;
02221                 case 'h':
02222                   g_string_append (outstr, MONTH_ABBR (datetime));
02223                   break;
02224                 case 'H':
02225                   g_string_append_printf (outstr, "%02d", g_date_time_get_hour (datetime));
02226                   break;
02227                 case 'I':
02228                   if (g_date_time_get_hour (datetime) == 0)
02229                     g_string_append (outstr, "12");
02230                   else
02231                     g_string_append_printf (outstr, "%02d", g_date_time_get_hour (datetime) % 12);
02232                   break;
02233                 case 'j':
02234                   g_string_append_printf (outstr, "%03d", g_date_time_get_day_of_year (datetime));
02235                   break;
02236                 case 'k':
02237                   g_string_append_printf (outstr, "%2d", g_date_time_get_hour (datetime));
02238                   break;
02239                 case 'l':
02240                   if (g_date_time_get_hour (datetime) == 0)
02241                     g_string_append (outstr, "12");
02242                   else
02243                     g_string_append_printf (outstr, "%2d", g_date_time_get_hour (datetime) % 12);
02244                   break;
02245                 case 'm':
02246                   g_string_append_printf (outstr, "%02d", g_date_time_get_month (datetime));
02247                   break;
02248                 case 'M':
02249                   g_string_append_printf (outstr, "%02d", g_date_time_get_minute (datetime));
02250                   break;
02251                 case 'N':
02252                   g_string_append_printf (outstr, "%"G_GUINT64_FORMAT, datetime->usec % USEC_PER_SECOND);
02253                   break;
02254                 case 'p':
02255                   g_string_append (outstr, GET_AMPM (datetime, FALSE));
02256                   break;
02257                 case 'P':
02258                   g_string_append (outstr, GET_AMPM (datetime, TRUE));
02259                   break;
02260                 case 'r':
02261                   {
02262                     gint hour = g_date_time_get_hour (datetime) % 12;
02263                     if (hour == 0)
02264                       hour = 12;
02265                     g_string_append_printf (outstr, "%02d:%02d:%02d %s",
02266                                             hour,
02267                                             g_date_time_get_minute (datetime),
02268                                             g_date_time_get_second (datetime),
02269                                             GET_AMPM (datetime, FALSE));
02270                   }
02271                   break;
02272                 case 'R':
02273                   g_string_append_printf (outstr, "%02d:%02d",
02274                                           g_date_time_get_hour (datetime),
02275                                           g_date_time_get_minute (datetime));
02276                   break;
02277                 case 's':
02278                   g_string_append_printf (outstr, "%" G_GINT64_FORMAT, g_date_time_to_unix (datetime));
02279                   break;
02280                 case 'S':
02281                   g_string_append_printf (outstr, "%02d", g_date_time_get_second (datetime));
02282                   break;
02283                 case 't':
02284                   g_string_append_c (outstr, '\t');
02285                   break;
02286                 case 'u':
02287                   g_string_append_printf (outstr, "%d", g_date_time_get_day_of_week (datetime));
02288                   break;
02289                 case 'W':
02290                   g_string_append_printf (outstr, "%d", g_date_time_get_day_of_year (datetime) / 7);
02291                   break;
02292                 case 'x':
02293                   {
02294                     tmp = GET_PREFERRED_DATE (datetime);
02295                     g_string_append (outstr, tmp);
02296                     g_free (tmp);
02297                   }
02298                   break;
02299                 case 'X':
02300                   {
02301                     tmp = GET_PREFERRED_TIME (datetime);
02302                     g_string_append (outstr, tmp);
02303                     g_free (tmp);
02304                   }
02305                   break;
02306                 case 'y':
02307                   g_string_append_printf (outstr, "%02d", g_date_time_get_year (datetime) % 100);
02308                   break;
02309                 case 'Y':
02310                   g_string_append_printf (outstr, "%d", g_date_time_get_year (datetime));
02311                   break;
02312                 case 'z':
02313                   if (datetime->tz != NULL)
02314                     {
02315                       gint64 offset = g_date_time_get_utc_offset (datetime)
02316                                     / USEC_PER_SECOND;
02317 
02318                       g_string_append_printf (outstr, "%c%02d%02d",
02319                                               offset >= 0 ? '+' : '-',
02320                                               (int) offset / 3600,
02321                                               (int) offset / 60 % 60);
02322                     }
02323                   else
02324                     g_string_append (outstr, "+0000");
02325                   break;
02326                 case 'Z':
02327                   g_string_append (outstr, g_date_time_get_timezone_abbreviation (datetime));
02328                   break;
02329                 case '%':
02330                   g_string_append_c (outstr, '%');
02331                   break;
02332                 case 'n':
02333                   g_string_append_c (outstr, '\n');
02334                   break;
02335                 default:
02336                   goto bad_format;
02337                 }
02338               in_mod = FALSE;
02339             }
02340           else
02341             g_string_append_unichar (outstr, c);
02342         }
02343     }
02344 
02345   return g_string_free (outstr, FALSE);
02346 
02347 bad_format:
02348   g_string_free (outstr, TRUE);
02349   return NULL;
02350 }
02351 
02352 
02353 /* Epilogue {{{1 */
02354 /* vim:set foldmethod=marker: */