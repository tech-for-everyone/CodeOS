/* GdkText
 * pango-script.h: Script tag handling
 *
 * Copyright (C) 2002 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __PANGO_SCRIPT_H__
#define __PANGO_SCRIPT_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GdkTextScriptIter:
 *
 * A `GdkTextScriptIter` is used to iterate through a string
 * and identify ranges in different scripts.
 **/
typedef struct _PangoScriptIter GdkTextScriptIter;

/**
 * GdkTextScript:
 * @GDK_TEXT_SCRIPT_INVALID_CODE: a value never returned from gdk_text_script_for_unichar()
 * @GDK_TEXT_SCRIPT_COMMON: a character used by multiple different scripts
 * @GDK_TEXT_SCRIPT_INHERITED: a mark glyph that takes its script from the
 * base glyph to which it is attached
 * @GDK_TEXT_SCRIPT_ARABIC:        Arabic
 * @GDK_TEXT_SCRIPT_ARMENIAN: Armenian
 * @GDK_TEXT_SCRIPT_BENGALI:       Bengali
 * @GDK_TEXT_SCRIPT_BOPOMOFO: Bopomofo
 * @GDK_TEXT_SCRIPT_CHEROKEE:      Cherokee
 * @GDK_TEXT_SCRIPT_COPTIC:        Coptic
 * @GDK_TEXT_SCRIPT_CYRILLIC:      Cyrillic
 * @GDK_TEXT_SCRIPT_DESERET:       Deseret
 * @GDK_TEXT_SCRIPT_DEVANAGARI:    Devanagari
 * @GDK_TEXT_SCRIPT_ETHIOPIC:      Ethiopic
 * @GDK_TEXT_SCRIPT_GEORGIAN:      Georgian
 * @GDK_TEXT_SCRIPT_GOTHIC:        Gothic
 * @GDK_TEXT_SCRIPT_GREEK:         Greek
 * @GDK_TEXT_SCRIPT_GUJARATI:      Gujarati
 * @GDK_TEXT_SCRIPT_GURMUKHI:      Gurmukhi
 * @GDK_TEXT_SCRIPT_HAN:   Han
 * @GDK_TEXT_SCRIPT_HANGUL:        Hangul
 * @GDK_TEXT_SCRIPT_HEBREW:        Hebrew
 * @GDK_TEXT_SCRIPT_HIRAGANA:      Hiragana
 * @GDK_TEXT_SCRIPT_KANNADA:       Kannada
 * @GDK_TEXT_SCRIPT_KATAKANA:      Katakana
 * @GDK_TEXT_SCRIPT_KHMER:         Khmer
 * @GDK_TEXT_SCRIPT_LAO:   Lao
 * @GDK_TEXT_SCRIPT_LATIN:         Latin
 * @GDK_TEXT_SCRIPT_MALAYALAM:     Malayalam
 * @GDK_TEXT_SCRIPT_MONGOLIAN:     Mongolian
 * @GDK_TEXT_SCRIPT_MYANMAR:       Myanmar
 * @GDK_TEXT_SCRIPT_OGHAM:         Ogham
 * @GDK_TEXT_SCRIPT_OLD_ITALIC:    Old Italic
 * @GDK_TEXT_SCRIPT_ORIYA:         Oriya
 * @GDK_TEXT_SCRIPT_RUNIC:         Runic
 * @GDK_TEXT_SCRIPT_SINHALA:       Sinhala
 * @GDK_TEXT_SCRIPT_SYRIAC:        Syriac
 * @GDK_TEXT_SCRIPT_TAMIL:         Tamil
 * @GDK_TEXT_SCRIPT_TELUGU:        Telugu
 * @GDK_TEXT_SCRIPT_THAANA:        Thaana
 * @GDK_TEXT_SCRIPT_THAI:  Thai
 * @GDK_TEXT_SCRIPT_TIBETAN:       Tibetan
 * @GDK_TEXT_SCRIPT_CANADIAN_ABORIGINAL:   Canadian Aboriginal
 * @GDK_TEXT_SCRIPT_YI:    Yi
 * @GDK_TEXT_SCRIPT_TAGALOG:       Tagalog
 * @GDK_TEXT_SCRIPT_HANUNOO:       Hanunoo
 * @GDK_TEXT_SCRIPT_BUHID:         Buhid
 * @GDK_TEXT_SCRIPT_TAGBANWA:      Tagbanwa
 * @GDK_TEXT_SCRIPT_BRAILLE:       Braille
 * @GDK_TEXT_SCRIPT_CYPRIOT:       Cypriot
 * @GDK_TEXT_SCRIPT_LIMBU:         Limbu
 * @GDK_TEXT_SCRIPT_OSMANYA:       Osmanya
 * @GDK_TEXT_SCRIPT_SHAVIAN:       Shavian
 * @GDK_TEXT_SCRIPT_LINEAR_B:      Linear B
 * @GDK_TEXT_SCRIPT_TAI_LE:        Tai Le
 * @GDK_TEXT_SCRIPT_UGARITIC:      Ugaritic
 * @GDK_TEXT_SCRIPT_NEW_TAI_LUE:   New Tai Lue. Since 1.10
 * @GDK_TEXT_SCRIPT_BUGINESE:      Buginese. Since 1.10
 * @GDK_TEXT_SCRIPT_GLAGOLITIC:    Glagolitic. Since 1.10
 * @GDK_TEXT_SCRIPT_TIFINAGH:      Tifinagh. Since 1.10
 * @GDK_TEXT_SCRIPT_SYLOTI_NAGRI:  Syloti Nagri. Since 1.10
 * @GDK_TEXT_SCRIPT_OLD_PERSIAN:   Old Persian. Since 1.10
 * @GDK_TEXT_SCRIPT_KHAROSHTHI:    Kharoshthi. Since 1.10
 * @GDK_TEXT_SCRIPT_UNKNOWN:               an unassigned code point. Since 1.14
 * @GDK_TEXT_SCRIPT_BALINESE:              Balinese. Since 1.14
 * @GDK_TEXT_SCRIPT_CUNEIFORM:     Cuneiform. Since 1.14
 * @GDK_TEXT_SCRIPT_PHOENICIAN:    Phoenician. Since 1.14
 * @GDK_TEXT_SCRIPT_PHAGS_PA:              Phags-pa. Since 1.14
 * @GDK_TEXT_SCRIPT_NKO:           N'Ko. Since 1.14
 * @GDK_TEXT_SCRIPT_KAYAH_LI:   Kayah Li. Since 1.20.1
 * @GDK_TEXT_SCRIPT_LEPCHA:     Lepcha. Since 1.20.1
 * @GDK_TEXT_SCRIPT_REJANG:     Rejang. Since 1.20.1
 * @GDK_TEXT_SCRIPT_SUNDANESE:  Sundanese. Since 1.20.1
 * @GDK_TEXT_SCRIPT_SAURASHTRA: Saurashtra. Since 1.20.1
 * @GDK_TEXT_SCRIPT_CHAM:       Cham. Since 1.20.1
 * @GDK_TEXT_SCRIPT_OL_CHIKI:   Ol Chiki. Since 1.20.1
 * @GDK_TEXT_SCRIPT_VAI:        Vai. Since 1.20.1
 * @GDK_TEXT_SCRIPT_CARIAN:     Carian. Since 1.20.1
 * @GDK_TEXT_SCRIPT_LYCIAN:     Lycian. Since 1.20.1
 * @GDK_TEXT_SCRIPT_LYDIAN:     Lydian. Since 1.20.1
 * @GDK_TEXT_SCRIPT_BATAK:      Batak. Since 1.32
 * @GDK_TEXT_SCRIPT_BRAHMI:     Brahmi. Since 1.32
 * @GDK_TEXT_SCRIPT_MANDAIC:    Mandaic. Since 1.32
 * @GDK_TEXT_SCRIPT_CHAKMA:               Chakma. Since: 1.32
 * @GDK_TEXT_SCRIPT_MEROITIC_CURSIVE:     Meroitic Cursive. Since: 1.32
 * @GDK_TEXT_SCRIPT_MEROITIC_HIEROGLYPHS: Meroitic Hieroglyphs. Since: 1.32
 * @GDK_TEXT_SCRIPT_MIAO:                 Miao. Since: 1.32
 * @GDK_TEXT_SCRIPT_SHARADA:              Sharada. Since: 1.32
 * @GDK_TEXT_SCRIPT_SORA_SOMPENG:         Sora Sompeng. Since: 1.32
 * @GDK_TEXT_SCRIPT_TAKRI:                Takri. Since: 1.32
 * @GDK_TEXT_SCRIPT_BASSA_VAH:            Bassa. Since: 1.40
 * @GDK_TEXT_SCRIPT_CAUCASIAN_ALBANIAN:   Caucasian Albanian. Since: 1.40
 * @GDK_TEXT_SCRIPT_DUPLOYAN:             Duployan. Since: 1.40
 * @GDK_TEXT_SCRIPT_ELBASAN:              Elbasan. Since: 1.40
 * @GDK_TEXT_SCRIPT_GRANTHA:              Grantha. Since: 1.40
 * @GDK_TEXT_SCRIPT_KHOJKI:               Kjohki. Since: 1.40
 * @GDK_TEXT_SCRIPT_KHUDAWADI:            Khudawadi, Sindhi. Since: 1.40
 * @GDK_TEXT_SCRIPT_LINEAR_A:             Linear A. Since: 1.40
 * @GDK_TEXT_SCRIPT_MAHAJANI:             Mahajani. Since: 1.40
 * @GDK_TEXT_SCRIPT_MANICHAEAN:           Manichaean. Since: 1.40
 * @GDK_TEXT_SCRIPT_MENDE_KIKAKUI:        Mende Kikakui. Since: 1.40
 * @GDK_TEXT_SCRIPT_MODI:                 Modi. Since: 1.40
 * @GDK_TEXT_SCRIPT_MRO:                  Mro. Since: 1.40
 * @GDK_TEXT_SCRIPT_NABATAEAN:            Nabataean. Since: 1.40
 * @GDK_TEXT_SCRIPT_OLD_NORTH_ARABIAN:    Old North Arabian. Since: 1.40
 * @GDK_TEXT_SCRIPT_OLD_PERMIC:           Old Permic. Since: 1.40
 * @GDK_TEXT_SCRIPT_PAHAWH_HMONG:         Pahawh Hmong. Since: 1.40
 * @GDK_TEXT_SCRIPT_PALMYRENE:            Palmyrene. Since: 1.40
 * @GDK_TEXT_SCRIPT_PAU_CIN_HAU:          Pau Cin Hau. Since: 1.40
 * @GDK_TEXT_SCRIPT_PSALTER_PAHLAVI:      Psalter Pahlavi. Since: 1.40
 * @GDK_TEXT_SCRIPT_SIDDHAM:              Siddham. Since: 1.40
 * @GDK_TEXT_SCRIPT_TIRHUTA:              Tirhuta. Since: 1.40
 * @GDK_TEXT_SCRIPT_WARANG_CITI:          Warang Citi. Since: 1.40
 * @GDK_TEXT_SCRIPT_AHOM:                 Ahom. Since: 1.40
 * @GDK_TEXT_SCRIPT_ANATOLIAN_HIEROGLYPHS: Anatolian Hieroglyphs. Since: 1.40
 * @GDK_TEXT_SCRIPT_HATRAN:               Hatran. Since: 1.40
 * @GDK_TEXT_SCRIPT_MULTANI:              Multani. Since: 1.40
 * @GDK_TEXT_SCRIPT_OLD_HUNGARIAN:        Old Hungarian. Since: 1.40
 * @GDK_TEXT_SCRIPT_SIGNWRITING:          Signwriting. Since: 1.40
 *
 * The `GdkTextScript` enumeration identifies different writing
 * systems.
 *
 * The values correspond to the names as defined in the Unicode standard. See
 * [Unicode Standard Annex 24: Script names](http://www.unicode.org/reports/tr24/)
 *
 * Note that this enumeration is deprecated and will not be updated to include values
 * in newer versions of the Unicode standard. Applications should use the
 * [enum@GLib.UnicodeScript] enumeration instead,
 * whose values are interchangeable with `GdkTextScript`.
 */
typedef enum {                         /* ISO 15924 code */
      GDK_TEXT_SCRIPT_INVALID_CODE = -1,
      GDK_TEXT_SCRIPT_COMMON       = 0,   /* Zyyy */
      GDK_TEXT_SCRIPT_INHERITED,          /* Qaai */
      GDK_TEXT_SCRIPT_ARABIC,             /* Arab */
      GDK_TEXT_SCRIPT_ARMENIAN,           /* Armn */
      GDK_TEXT_SCRIPT_BENGALI,            /* Beng */
      GDK_TEXT_SCRIPT_BOPOMOFO,           /* Bopo */
      GDK_TEXT_SCRIPT_CHEROKEE,           /* Cher */
      GDK_TEXT_SCRIPT_COPTIC,             /* Qaac */
      GDK_TEXT_SCRIPT_CYRILLIC,           /* Cyrl (Cyrs) */
      GDK_TEXT_SCRIPT_DESERET,            /* Dsrt */
      GDK_TEXT_SCRIPT_DEVANAGARI,         /* Deva */
      GDK_TEXT_SCRIPT_ETHIOPIC,           /* Ethi */
      GDK_TEXT_SCRIPT_GEORGIAN,           /* Geor (Geon, Geoa) */
      GDK_TEXT_SCRIPT_GOTHIC,             /* Goth */
      GDK_TEXT_SCRIPT_GREEK,              /* Grek */
      GDK_TEXT_SCRIPT_GUJARATI,           /* Gujr */
      GDK_TEXT_SCRIPT_GURMUKHI,           /* Guru */
      GDK_TEXT_SCRIPT_HAN,                /* Hani */
      GDK_TEXT_SCRIPT_HANGUL,             /* Hang */
      GDK_TEXT_SCRIPT_HEBREW,             /* Hebr */
      GDK_TEXT_SCRIPT_HIRAGANA,           /* Hira */
      GDK_TEXT_SCRIPT_KANNADA,            /* Knda */
      GDK_TEXT_SCRIPT_KATAKANA,           /* Kana */
      GDK_TEXT_SCRIPT_KHMER,              /* Khmr */
      GDK_TEXT_SCRIPT_LAO,                /* Laoo */
      GDK_TEXT_SCRIPT_LATIN,              /* Latn (Latf, Latg) */
      GDK_TEXT_SCRIPT_MALAYALAM,          /* Mlym */
      GDK_TEXT_SCRIPT_MONGOLIAN,          /* Mong */
      GDK_TEXT_SCRIPT_MYANMAR,            /* Mymr */
      GDK_TEXT_SCRIPT_OGHAM,              /* Ogam */
      GDK_TEXT_SCRIPT_OLD_ITALIC,         /* Ital */
      GDK_TEXT_SCRIPT_ORIYA,              /* Orya */
      GDK_TEXT_SCRIPT_RUNIC,              /* Runr */
      GDK_TEXT_SCRIPT_SINHALA,            /* Sinh */
      GDK_TEXT_SCRIPT_SYRIAC,             /* Syrc (Syrj, Syrn, Syre) */
      GDK_TEXT_SCRIPT_TAMIL,              /* Taml */
      GDK_TEXT_SCRIPT_TELUGU,             /* Telu */
      GDK_TEXT_SCRIPT_THAANA,             /* Thaa */
      GDK_TEXT_SCRIPT_THAI,               /* Thai */
      GDK_TEXT_SCRIPT_TIBETAN,            /* Tibt */
      GDK_TEXT_SCRIPT_CANADIAN_ABORIGINAL, /* Cans */
      GDK_TEXT_SCRIPT_YI,                 /* Yiii */
      GDK_TEXT_SCRIPT_TAGALOG,            /* Tglg */
      GDK_TEXT_SCRIPT_HANUNOO,            /* Hano */
      GDK_TEXT_SCRIPT_BUHID,              /* Buhd */
      GDK_TEXT_SCRIPT_TAGBANWA,           /* Tagb */

      /* Unicode-4.0 additions */
      GDK_TEXT_SCRIPT_BRAILLE,            /* Brai */
      GDK_TEXT_SCRIPT_CYPRIOT,            /* Cprt */
      GDK_TEXT_SCRIPT_LIMBU,              /* Limb */
      GDK_TEXT_SCRIPT_OSMANYA,            /* Osma */
      GDK_TEXT_SCRIPT_SHAVIAN,            /* Shaw */
      GDK_TEXT_SCRIPT_LINEAR_B,           /* Linb */
      GDK_TEXT_SCRIPT_TAI_LE,             /* Tale */
      GDK_TEXT_SCRIPT_UGARITIC,           /* Ugar */

      /* Unicode-4.1 additions */
      GDK_TEXT_SCRIPT_NEW_TAI_LUE,        /* Talu */
      GDK_TEXT_SCRIPT_BUGINESE,           /* Bugi */
      GDK_TEXT_SCRIPT_GLAGOLITIC,         /* Glag */
      GDK_TEXT_SCRIPT_TIFINAGH,           /* Tfng */
      GDK_TEXT_SCRIPT_SYLOTI_NAGRI,       /* Sylo */
      GDK_TEXT_SCRIPT_OLD_PERSIAN,        /* Xpeo */
      GDK_TEXT_SCRIPT_KHAROSHTHI,         /* Khar */

      /* Unicode-5.0 additions */
      GDK_TEXT_SCRIPT_UNKNOWN,            /* Zzzz */
      GDK_TEXT_SCRIPT_BALINESE,           /* Bali */
      GDK_TEXT_SCRIPT_CUNEIFORM,          /* Xsux */
      GDK_TEXT_SCRIPT_PHOENICIAN,         /* Phnx */
      GDK_TEXT_SCRIPT_PHAGS_PA,           /* Phag */
      GDK_TEXT_SCRIPT_NKO,                /* Nkoo */

      /* Unicode-5.1 additions */
      GDK_TEXT_SCRIPT_KAYAH_LI,           /* Kali */
      GDK_TEXT_SCRIPT_LEPCHA,             /* Lepc */
      GDK_TEXT_SCRIPT_REJANG,             /* Rjng */
      GDK_TEXT_SCRIPT_SUNDANESE,          /* Sund */
      GDK_TEXT_SCRIPT_SAURASHTRA,         /* Saur */
      GDK_TEXT_SCRIPT_CHAM,               /* Cham */
      GDK_TEXT_SCRIPT_OL_CHIKI,           /* Olck */
      GDK_TEXT_SCRIPT_VAI,                /* Vaii */
      GDK_TEXT_SCRIPT_CARIAN,             /* Cari */
      GDK_TEXT_SCRIPT_LYCIAN,             /* Lyci */
      GDK_TEXT_SCRIPT_LYDIAN,             /* Lydi */

      /* Unicode-6.0 additions */
      GDK_TEXT_SCRIPT_BATAK,              /* Batk */
      GDK_TEXT_SCRIPT_BRAHMI,             /* Brah */
      GDK_TEXT_SCRIPT_MANDAIC,            /* Mand */

      /* Unicode-6.1 additions */
      GDK_TEXT_SCRIPT_CHAKMA,             /* Cakm */
      GDK_TEXT_SCRIPT_MEROITIC_CURSIVE,   /* Merc */
      GDK_TEXT_SCRIPT_MEROITIC_HIEROGLYPHS,/* Mero */
      GDK_TEXT_SCRIPT_MIAO,               /* Plrd */
      GDK_TEXT_SCRIPT_SHARADA,            /* Shrd */
      GDK_TEXT_SCRIPT_SORA_SOMPENG,       /* Sora */
      GDK_TEXT_SCRIPT_TAKRI,              /* Takr */

      /* Unicode 7.0 additions */
      GDK_TEXT_SCRIPT_BASSA_VAH,              /* Bass */
      GDK_TEXT_SCRIPT_CAUCASIAN_ALBANIAN,     /* Aghb */
      GDK_TEXT_SCRIPT_DUPLOYAN,               /* Dupl */
      GDK_TEXT_SCRIPT_ELBASAN,                /* Elba */
      GDK_TEXT_SCRIPT_GRANTHA,                /* Gran */
      GDK_TEXT_SCRIPT_KHOJKI,                 /* Khoj */
      GDK_TEXT_SCRIPT_KHUDAWADI,              /* Sind */
      GDK_TEXT_SCRIPT_LINEAR_A,               /* Lina */
      GDK_TEXT_SCRIPT_MAHAJANI,               /* Mahj */
      GDK_TEXT_SCRIPT_MANICHAEAN,             /* Manu */
      GDK_TEXT_SCRIPT_MENDE_KIKAKUI,          /* Mend */
      GDK_TEXT_SCRIPT_MODI,                   /* Modi */
      GDK_TEXT_SCRIPT_MRO,                    /* Mroo */
      GDK_TEXT_SCRIPT_NABATAEAN,              /* Nbat */
      GDK_TEXT_SCRIPT_OLD_NORTH_ARABIAN,      /* Narb */
      GDK_TEXT_SCRIPT_OLD_PERMIC,             /* Perm */
      GDK_TEXT_SCRIPT_PAHAWH_HMONG,           /* Hmng */
      GDK_TEXT_SCRIPT_PALMYRENE,              /* Palm */
      GDK_TEXT_SCRIPT_PAU_CIN_HAU,            /* Pauc */
      GDK_TEXT_SCRIPT_PSALTER_PAHLAVI,        /* Phlp */
      GDK_TEXT_SCRIPT_SIDDHAM,                /* Sidd */
      GDK_TEXT_SCRIPT_TIRHUTA,                /* Tirh */
      GDK_TEXT_SCRIPT_WARANG_CITI,            /* Wara */

      /* Unicode 8.0 additions */
      GDK_TEXT_SCRIPT_AHOM,                   /* Ahom */
      GDK_TEXT_SCRIPT_ANATOLIAN_HIEROGLYPHS,  /* Hluw */
      GDK_TEXT_SCRIPT_HATRAN,                 /* Hatr */
      GDK_TEXT_SCRIPT_MULTANI,                /* Mult */
      GDK_TEXT_SCRIPT_OLD_HUNGARIAN,          /* Hung */
      GDK_TEXT_SCRIPT_SIGNWRITING             /* Sgnw */
} GdkTextScript;

#include <pango/pango-version-macros.h>

GDK_TEXT_DEPRECATED_IN_1_44_FOR(g_unichar_get_script)
GdkTextScript gdk_text_script_for_unichar         (gunichar             ch) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_44
GType            gdk_text_script_iter_get_type  (void) G_GNUC_CONST;

GDK_TEXT_AVAILABLE_IN_1_4
GdkTextScriptIter *gdk_text_script_iter_new       (const char          *text,
					      int                  length);
GDK_TEXT_AVAILABLE_IN_1_4
void             gdk_text_script_iter_get_range (GdkTextScriptIter     *iter,
                                              const char         **start,
                                              const char         **end,
                                              GdkTextScript         *script);
GDK_TEXT_AVAILABLE_IN_1_4
gboolean         gdk_text_script_iter_next      (GdkTextScriptIter     *iter);
GDK_TEXT_AVAILABLE_IN_1_4
void             gdk_text_script_iter_free      (GdkTextScriptIter     *iter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GdkTextScriptIter, gdk_text_script_iter_free)

#include <pango/pango-language.h>

GDK_TEXT_AVAILABLE_IN_1_4
GdkTextLanguage *gdk_text_script_get_sample_language (GdkTextScript    script) G_GNUC_PURE;

G_END_DECLS

#endif /* __PANGO_SCRIPT_H__ */
