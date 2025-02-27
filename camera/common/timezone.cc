/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros-camera/timezone.h"

#include <stddef.h>
#include <string.h>

#include <iterator>
#include <map>

#include <base/files/file_util.h>
#include <base/no_destructor.h>
#include <base/strings/utf_string_conversions.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

const char kTimeZoneFilePath[] = "/etc/localtime";
const char kZoneInfoFilePath[] = "/usr/share/zoneinfo/";

class TimezoneMap {
 public:
  TimezoneMap(const TimezoneMap&) = delete;
  TimezoneMap& operator=(const TimezoneMap&) = delete;
  static TimezoneMap* GetInstance() {
    static base::NoDestructor<TimezoneMap> instance;
    return instance.get();
  }

  std::string CountryCodeForTimezone(const std::string& olson_code) {
    std::map<const char*, const char*, CompareCStrings>::iterator iter =
        map_.find(olson_code.c_str());
    if (iter != map_.end()) {
      return iter->second;
    }

    return std::string();
  }

  TimezoneMap() {
    // These mappings are adapted from zone.tab, which is available at
    // <http://www.ietf.org/timezones/data/zone.tab> and is a part of public
    // domain.
    struct OlsonCodeData {
      const char* country_code;
      const char* olson_code;
    };
    static const OlsonCodeData olson_code_data[] = {
        {"AD", "Europe/Andorra"},
        {"AE", "Asia/Dubai"},
        {"AF", "Asia/Kabul"},
        {"AG", "America/Antigua"},
        {"AI", "America/Anguilla"},
        {"AL", "Europe/Tirane"},
        {"AM", "Asia/Yerevan"},
        {"AO", "Africa/Luanda"},
        {"AQ", "Antarctica/McMurdo"},
        {"AQ", "Antarctica/Rothera"},
        {"AQ", "Antarctica/Palmer"},
        {"AQ", "Antarctica/Mawson"},
        {"AQ", "Antarctica/Davis"},
        {"AQ", "Antarctica/Casey"},
        {"AQ", "Antarctica/Vostok"},
        {"AQ", "Antarctica/DumontDUrville"},
        {"AQ", "Antarctica/Syowa"},
        {"AR", "America/Argentina/Buenos_Aires"},
        {"AR", "America/Argentina/Cordoba"},
        {"AR", "America/Argentina/Salta"},
        {"AR", "America/Argentina/Jujuy"},
        {"AR", "America/Argentina/Tucuman"},
        {"AR", "America/Argentina/Catamarca"},
        {"AR", "America/Argentina/La_Rioja"},
        {"AR", "America/Argentina/San_Juan"},
        {"AR", "America/Argentina/Mendoza"},
        {"AR", "America/Argentina/San_Luis"},
        {"AR", "America/Argentina/Rio_Gallegos"},
        {"AR", "America/Argentina/Ushuaia"},
        {"AS", "Pacific/Pago_Pago"},
        {"AT", "Europe/Vienna"},
        {"AU", "Australia/Lord_Howe"},
        {"AU", "Antarctica/Macquarie"},
        {"AU", "Australia/Hobart"},
        {"AU", "Australia/Currie"},
        {"AU", "Australia/Melbourne"},
        {"AU", "Australia/Sydney"},
        {"AU", "Australia/Broken_Hill"},
        {"AU", "Australia/Brisbane"},
        {"AU", "Australia/Lindeman"},
        {"AU", "Australia/Adelaide"},
        {"AU", "Australia/Darwin"},
        {"AU", "Australia/Perth"},
        {"AU", "Australia/Eucla"},
        {"AW", "America/Aruba"},
        {"AX", "Europe/Mariehamn"},
        {"AZ", "Asia/Baku"},
        {"BA", "Europe/Sarajevo"},
        {"BB", "America/Barbados"},
        {"BD", "Asia/Dhaka"},
        {"BE", "Europe/Brussels"},
        {"BF", "Africa/Ouagadougou"},
        {"BG", "Europe/Sofia"},
        {"BH", "Asia/Bahrain"},
        {"BI", "Africa/Bujumbura"},
        {"BJ", "Africa/Porto-Novo"},
        {"BL", "America/St_Barthelemy"},
        {"BM", "Atlantic/Bermuda"},
        {"BN", "Asia/Brunei"},
        {"BO", "America/La_Paz"},
        {"BQ", "America/Kralendijk"},
        {"BR", "America/Noronha"},
        {"BR", "America/Belem"},
        {"BR", "America/Fortaleza"},
        {"BR", "America/Recife"},
        {"BR", "America/Araguaina"},
        {"BR", "America/Maceio"},
        {"BR", "America/Bahia"},
        {"BR", "America/Sao_Paulo"},
        {"BR", "America/Campo_Grande"},
        {"BR", "America/Cuiaba"},
        {"BR", "America/Santarem"},
        {"BR", "America/Porto_Velho"},
        {"BR", "America/Boa_Vista"},
        {"BR", "America/Manaus"},
        {"BR", "America/Eirunepe"},
        {"BR", "America/Rio_Branco"},
        {"BS", "America/Nassau"},
        {"BT", "Asia/Thimphu"},
        {"BW", "Africa/Gaborone"},
        {"BY", "Europe/Minsk"},
        {"BZ", "America/Belize"},
        {"CA", "America/St_Johns"},
        {"CA", "America/Halifax"},
        {"CA", "America/Glace_Bay"},
        {"CA", "America/Moncton"},
        {"CA", "America/Goose_Bay"},
        {"CA", "America/Blanc-Sablon"},
        {"CA", "America/Toronto"},
        {"CA", "America/Nipigon"},
        {"CA", "America/Thunder_Bay"},
        {"CA", "America/Iqaluit"},
        {"CA", "America/Pangnirtung"},
        {"CA", "America/Resolute"},
        {"CA", "America/Atikokan"},
        {"CA", "America/Rankin_Inlet"},
        {"CA", "America/Winnipeg"},
        {"CA", "America/Rainy_River"},
        {"CA", "America/Regina"},
        {"CA", "America/Swift_Current"},
        {"CA", "America/Edmonton"},
        {"CA", "America/Cambridge_Bay"},
        {"CA", "America/Yellowknife"},
        {"CA", "America/Inuvik"},
        {"CA", "America/Creston"},
        {"CA", "America/Dawson_Creek"},
        {"CA", "America/Vancouver"},
        {"CA", "America/Whitehorse"},
        {"CA", "America/Dawson"},
        {"CC", "Indian/Cocos"},
        {"CD", "Africa/Kinshasa"},
        {"CD", "Africa/Lubumbashi"},
        {"CF", "Africa/Bangui"},
        {"CG", "Africa/Brazzaville"},
        {"CH", "Europe/Zurich"},
        {"CI", "Africa/Abidjan"},
        {"CK", "Pacific/Rarotonga"},
        {"CL", "America/Santiago"},
        {"CL", "Pacific/Easter"},
        {"CM", "Africa/Douala"},
        {"CN", "Asia/Shanghai"},
        {"CN", "Asia/Harbin"},
        {"CN", "Asia/Chongqing"},
        {"CN", "Asia/Urumqi"},
        {"CN", "Asia/Kashgar"},
        {"CO", "America/Bogota"},
        {"CR", "America/Costa_Rica"},
        {"CU", "America/Havana"},
        {"CV", "Atlantic/Cape_Verde"},
        {"CW", "America/Curacao"},
        {"CX", "Indian/Christmas"},
        {"CY", "Asia/Nicosia"},
        {"CZ", "Europe/Prague"},
        {"DE", "Europe/Berlin"},
        {"DE", "Europe/Busingen"},
        {"DJ", "Africa/Djibouti"},
        {"DK", "Europe/Copenhagen"},
        {"DM", "America/Dominica"},
        {"DO", "America/Santo_Domingo"},
        {"DZ", "Africa/Algiers"},
        {"EC", "America/Guayaquil"},
        {"EC", "Pacific/Galapagos"},
        {"EE", "Europe/Tallinn"},
        {"EG", "Africa/Cairo"},
        {"EH", "Africa/El_Aaiun"},
        {"ER", "Africa/Asmara"},
        {"ES", "Europe/Madrid"},
        {"ES", "Africa/Ceuta"},
        {"ES", "Atlantic/Canary"},
        {"ET", "Africa/Addis_Ababa"},
        {"FI", "Europe/Helsinki"},
        {"FJ", "Pacific/Fiji"},
        {"FK", "Atlantic/Stanley"},
        {"FM", "Pacific/Chuuk"},
        {"FM", "Pacific/Pohnpei"},
        {"FM", "Pacific/Kosrae"},
        {"FO", "Atlantic/Faroe"},
        {"FR", "Europe/Paris"},
        {"GA", "Africa/Libreville"},
        {"GB", "Europe/London"},
        {"GD", "America/Grenada"},
        {"GE", "Asia/Tbilisi"},
        {"GF", "America/Cayenne"},
        {"GG", "Europe/Guernsey"},
        {"GH", "Africa/Accra"},
        {"GI", "Europe/Gibraltar"},
        {"GL", "America/Godthab"},
        {"GL", "America/Danmarkshavn"},
        {"GL", "America/Scoresbysund"},
        {"GL", "America/Thule"},
        {"GM", "Africa/Banjul"},
        {"GN", "Africa/Conakry"},
        {"GP", "America/Guadeloupe"},
        {"GQ", "Africa/Malabo"},
        {"GR", "Europe/Athens"},
        {"GS", "Atlantic/South_Georgia"},
        {"GT", "America/Guatemala"},
        {"GU", "Pacific/Guam"},
        {"GW", "Africa/Bissau"},
        {"GY", "America/Guyana"},
        {"HK", "Asia/Hong_Kong"},
        {"HN", "America/Tegucigalpa"},
        {"HR", "Europe/Zagreb"},
        {"HT", "America/Port-au-Prince"},
        {"HU", "Europe/Budapest"},
        {"ID", "Asia/Jakarta"},
        {"ID", "Asia/Pontianak"},
        {"ID", "Asia/Makassar"},
        {"ID", "Asia/Jayapura"},
        {"IE", "Europe/Dublin"},
        {"IL", "Asia/Jerusalem"},
        {"IM", "Europe/Isle_of_Man"},
        {"IN", "Asia/Kolkata"},
        {"IO", "Indian/Chagos"},
        {"IQ", "Asia/Baghdad"},
        {"IR", "Asia/Tehran"},
        {"IS", "Atlantic/Reykjavik"},
        {"IT", "Europe/Rome"},
        {"JE", "Europe/Jersey"},
        {"JM", "America/Jamaica"},
        {"JO", "Asia/Amman"},
        {"JP", "Asia/Tokyo"},
        {"KE", "Africa/Nairobi"},
        {"KG", "Asia/Bishkek"},
        {"KH", "Asia/Phnom_Penh"},
        {"KI", "Pacific/Tarawa"},
        {"KI", "Pacific/Enderbury"},
        {"KI", "Pacific/Kiritimati"},
        {"KM", "Indian/Comoro"},
        {"KN", "America/St_Kitts"},
        {"KP", "Asia/Pyongyang"},
        {"KR", "Asia/Seoul"},
        {"KW", "Asia/Kuwait"},
        {"KY", "America/Cayman"},
        {"KZ", "Asia/Almaty"},
        {"KZ", "Asia/Qyzylorda"},
        {"KZ", "Asia/Aqtobe"},
        {"KZ", "Asia/Aqtau"},
        {"KZ", "Asia/Oral"},
        {"LA", "Asia/Vientiane"},
        {"LB", "Asia/Beirut"},
        {"LC", "America/St_Lucia"},
        {"LI", "Europe/Vaduz"},
        {"LK", "Asia/Colombo"},
        {"LR", "Africa/Monrovia"},
        {"LS", "Africa/Maseru"},
        {"LT", "Europe/Vilnius"},
        {"LU", "Europe/Luxembourg"},
        {"LV", "Europe/Riga"},
        {"LY", "Africa/Tripoli"},
        {"MA", "Africa/Casablanca"},
        {"MC", "Europe/Monaco"},
        {"MD", "Europe/Chisinau"},
        {"ME", "Europe/Podgorica"},
        {"MF", "America/Marigot"},
        {"MG", "Indian/Antananarivo"},
        {"MH", "Pacific/Majuro"},
        {"MH", "Pacific/Kwajalein"},
        {"MK", "Europe/Skopje"},
        {"ML", "Africa/Bamako"},
        {"MM", "Asia/Rangoon"},
        {"MN", "Asia/Ulaanbaatar"},
        {"MN", "Asia/Hovd"},
        {"MN", "Asia/Choibalsan"},
        {"MO", "Asia/Macau"},
        {"MP", "Pacific/Saipan"},
        {"MQ", "America/Martinique"},
        {"MR", "Africa/Nouakchott"},
        {"MS", "America/Montserrat"},
        {"MT", "Europe/Malta"},
        {"MU", "Indian/Mauritius"},
        {"MV", "Indian/Maldives"},
        {"MW", "Africa/Blantyre"},
        {"MX", "America/Mexico_City"},
        {"MX", "America/Cancun"},
        {"MX", "America/Merida"},
        {"MX", "America/Monterrey"},
        {"MX", "America/Matamoros"},
        {"MX", "America/Mazatlan"},
        {"MX", "America/Chihuahua"},
        {"MX", "America/Ojinaga"},
        {"MX", "America/Hermosillo"},
        {"MX", "America/Tijuana"},
        {"MX", "America/Santa_Isabel"},
        {"MX", "America/Bahia_Banderas"},
        {"MY", "Asia/Kuala_Lumpur"},
        {"MY", "Asia/Kuching"},
        {"MZ", "Africa/Maputo"},
        {"NA", "Africa/Windhoek"},
        {"NC", "Pacific/Noumea"},
        {"NE", "Africa/Niamey"},
        {"NF", "Pacific/Norfolk"},
        {"NG", "Africa/Lagos"},
        {"NI", "America/Managua"},
        {"NL", "Europe/Amsterdam"},
        {"NO", "Europe/Oslo"},
        {"NP", "Asia/Kathmandu"},
        {"NR", "Pacific/Nauru"},
        {"NU", "Pacific/Niue"},
        {"NZ", "Pacific/Auckland"},
        {"NZ", "Pacific/Chatham"},
        {"OM", "Asia/Muscat"},
        {"PA", "America/Panama"},
        {"PE", "America/Lima"},
        {"PF", "Pacific/Tahiti"},
        {"PF", "Pacific/Marquesas"},
        {"PF", "Pacific/Gambier"},
        {"PG", "Pacific/Port_Moresby"},
        {"PH", "Asia/Manila"},
        {"PK", "Asia/Karachi"},
        {"PL", "Europe/Warsaw"},
        {"PM", "America/Miquelon"},
        {"PN", "Pacific/Pitcairn"},
        {"PR", "America/Puerto_Rico"},
        {"PS", "Asia/Gaza"},
        {"PS", "Asia/Hebron"},
        {"PT", "Europe/Lisbon"},
        {"PT", "Atlantic/Madeira"},
        {"PT", "Atlantic/Azores"},
        {"PW", "Pacific/Palau"},
        {"PY", "America/Asuncion"},
        {"QA", "Asia/Qatar"},
        {"RE", "Indian/Reunion"},
        {"RO", "Europe/Bucharest"},
        {"RS", "Europe/Belgrade"},
        {"RU", "Europe/Kaliningrad"},
        {"RU", "Europe/Moscow"},
        {"RU", "Europe/Volgograd"},
        {"RU", "Europe/Samara"},
        {"RU", "Asia/Yekaterinburg"},
        {"RU", "Asia/Omsk"},
        {"RU", "Asia/Novosibirsk"},
        {"RU", "Asia/Novokuznetsk"},
        {"RU", "Asia/Krasnoyarsk"},
        {"RU", "Asia/Irkutsk"},
        {"RU", "Asia/Yakutsk"},
        {"RU", "Asia/Khandyga"},
        {"RU", "Asia/Vladivostok"},
        {"RU", "Asia/Sakhalin"},
        {"RU", "Asia/Ust-Nera"},
        {"RU", "Asia/Magadan"},
        {"RU", "Asia/Kamchatka"},
        {"RU", "Asia/Anadyr"},
        {"RW", "Africa/Kigali"},
        {"SA", "Asia/Riyadh"},
        {"SB", "Pacific/Guadalcanal"},
        {"SC", "Indian/Mahe"},
        {"SD", "Africa/Khartoum"},
        {"SE", "Europe/Stockholm"},
        {"SG", "Asia/Singapore"},
        {"SH", "Atlantic/St_Helena"},
        {"SI", "Europe/Ljubljana"},
        {"SJ", "Arctic/Longyearbyen"},
        {"SK", "Europe/Bratislava"},
        {"SL", "Africa/Freetown"},
        {"SM", "Europe/San_Marino"},
        {"SN", "Africa/Dakar"},
        {"SO", "Africa/Mogadishu"},
        {"SR", "America/Paramaribo"},
        {"SS", "Africa/Juba"},
        {"ST", "Africa/Sao_Tome"},
        {"SV", "America/El_Salvador"},
        {"SX", "America/Lower_Princes"},
        {"SY", "Asia/Damascus"},
        {"SZ", "Africa/Mbabane"},
        {"TC", "America/Grand_Turk"},
        {"TD", "Africa/Ndjamena"},
        {"TF", "Indian/Kerguelen"},
        {"TG", "Africa/Lome"},
        {"TH", "Asia/Bangkok"},
        {"TJ", "Asia/Dushanbe"},
        {"TK", "Pacific/Fakaofo"},
        {"TL", "Asia/Dili"},
        {"TM", "Asia/Ashgabat"},
        {"TN", "Africa/Tunis"},
        {"TO", "Pacific/Tongatapu"},
        {"TR", "Europe/Istanbul"},
        {"TT", "America/Port_of_Spain"},
        {"TV", "Pacific/Funafuti"},
        {"TW", "Asia/Taipei"},
        {"TZ", "Africa/Dar_es_Salaam"},
        {"UA", "Europe/Kiev"},
        {"UA", "Europe/Uzhgorod"},
        {"UA", "Europe/Zaporozhye"},
        {"UA", "Europe/Simferopol"},
        {"UG", "Africa/Kampala"},
        {"UM", "Pacific/Johnston"},
        {"UM", "Pacific/Midway"},
        {"UM", "Pacific/Wake"},
        {"US", "America/New_York"},
        {"US", "America/Detroit"},
        {"US", "America/Kentucky/Louisville"},
        {"US", "America/Kentucky/Monticello"},
        {"US", "America/Indiana/Indianapolis"},
        {"US", "America/Indiana/Vincennes"},
        {"US", "America/Indiana/Winamac"},
        {"US", "America/Indiana/Marengo"},
        {"US", "America/Indiana/Petersburg"},
        {"US", "America/Indiana/Vevay"},
        {"US", "America/Chicago"},
        {"US", "America/Indiana/Tell_City"},
        {"US", "America/Indiana/Knox"},
        {"US", "America/Menominee"},
        {"US", "America/North_Dakota/Center"},
        {"US", "America/North_Dakota/New_Salem"},
        {"US", "America/North_Dakota/Beulah"},
        {"US", "America/Denver"},
        {"US", "America/Boise"},
        {"US", "America/Phoenix"},
        {"US", "America/Los_Angeles"},
        {"US", "America/Anchorage"},
        {"US", "America/Juneau"},
        {"US", "America/Sitka"},
        {"US", "America/Yakutat"},
        {"US", "America/Nome"},
        {"US", "America/Adak"},
        {"US", "America/Metlakatla"},
        {"US", "Pacific/Honolulu"},
        {"UY", "America/Montevideo"},
        {"UZ", "Asia/Samarkand"},
        {"UZ", "Asia/Tashkent"},
        {"VA", "Europe/Vatican"},
        {"VC", "America/St_Vincent"},
        {"VE", "America/Caracas"},
        {"VG", "America/Tortola"},
        {"VI", "America/St_Thomas"},
        {"VN", "Asia/Ho_Chi_Minh"},
        {"VU", "Pacific/Efate"},
        {"WF", "Pacific/Wallis"},
        {"WS", "Pacific/Apia"},
        {"YE", "Asia/Aden"},
        {"YT", "Indian/Mayotte"},
        {"ZA", "Africa/Johannesburg"},
        {"ZM", "Africa/Lusaka"},
        {"ZW", "Africa/Harare"},
        // The mappings below are custom additions to zone.tab.
        {"GB", "Etc/GMT"},
        {"GB", "Etc/UTC"},
        {"GB", "Etc/UCT"},
    };

    for (auto const& code_data : olson_code_data) {
      map_[code_data.olson_code] = code_data.country_code;
    }

    // These are mapping from old codenames to new codenames. They are also
    // part of public domain, and available at
    // <http://www.ietf.org/timezones/data/backward>.
    struct LinkData {
      const char* old_code;
      const char* new_code;
    };
    static const LinkData link_data[] = {
        {"Africa/Asmera", "Africa/Asmara"},
        {"Africa/Timbuktu", "Africa/Bamako"},
        {"America/Argentina/ComodRivadavia", "America/Argentina/Catamarca"},
        {"America/Atka", "America/Adak"},
        {"America/Buenos_Aires", "America/Argentina/Buenos_Aires"},
        {"America/Catamarca", "America/Argentina/Catamarca"},
        {"America/Coral_Harbour", "America/Atikokan"},
        {"America/Cordoba", "America/Argentina/Cordoba"},
        {"America/Ensenada", "America/Tijuana"},
        {"America/Fort_Wayne", "America/Indiana/Indianapolis"},
        {"America/Indianapolis", "America/Indiana/Indianapolis"},
        {"America/Jujuy", "America/Argentina/Jujuy"},
        {"America/Knox_IN", "America/Indiana/Knox"},
        {"America/Louisville", "America/Kentucky/Louisville"},
        {"America/Mendoza", "America/Argentina/Mendoza"},
        {"America/Porto_Acre", "America/Rio_Branco"},
        {"America/Rosario", "America/Argentina/Cordoba"},
        {"America/Virgin", "America/St_Thomas"},
        {"Asia/Ashkhabad", "Asia/Ashgabat"},
        {"Asia/Chungking", "Asia/Chongqing"},
        {"Asia/Dacca", "Asia/Dhaka"},
        {"Asia/Katmandu", "Asia/Kathmandu"},
        {"Asia/Calcutta", "Asia/Kolkata"},
        {"Asia/Macao", "Asia/Macau"},
        {"Asia/Tel_Aviv", "Asia/Jerusalem"},
        {"Asia/Saigon", "Asia/Ho_Chi_Minh"},
        {"Asia/Thimbu", "Asia/Thimphu"},
        {"Asia/Ujung_Pandang", "Asia/Makassar"},
        {"Asia/Ulan_Bator", "Asia/Ulaanbaatar"},
        {"Atlantic/Faeroe", "Atlantic/Faroe"},
        {"Atlantic/Jan_Mayen", "Europe/Oslo"},
        {"Australia/ACT", "Australia/Sydney"},
        {"Australia/Canberra", "Australia/Sydney"},
        {"Australia/LHI", "Australia/Lord_Howe"},
        {"Australia/NSW", "Australia/Sydney"},
        {"Australia/North", "Australia/Darwin"},
        {"Australia/Queensland", "Australia/Brisbane"},
        {"Australia/South", "Australia/Adelaide"},
        {"Australia/Tasmania", "Australia/Hobart"},
        {"Australia/Victoria", "Australia/Melbourne"},
        {"Australia/West", "Australia/Perth"},
        {"Australia/Yancowinna", "Australia/Broken_Hill"},
        {"Brazil/Acre", "America/Rio_Branco"},
        {"Brazil/DeNoronha", "America/Noronha"},
        {"Brazil/East", "America/Sao_Paulo"},
        {"Brazil/West", "America/Manaus"},
        {"Canada/Atlantic", "America/Halifax"},
        {"Canada/Central", "America/Winnipeg"},
        {"Canada/East-Saskatchewan", "America/Regina"},
        {"Canada/Eastern", "America/Toronto"},
        {"Canada/Mountain", "America/Edmonton"},
        {"Canada/Newfoundland", "America/St_Johns"},
        {"Canada/Pacific", "America/Vancouver"},
        {"Canada/Saskatchewan", "America/Regina"},
        {"Canada/Yukon", "America/Whitehorse"},
        {"Chile/Continental", "America/Santiago"},
        {"Chile/EasterIsland", "Pacific/Easter"},
        {"Cuba", "America/Havana"},
        {"Egypt", "Africa/Cairo"},
        {"Eire", "Europe/Dublin"},
        {"Europe/Belfast", "Europe/London"},
        {"Europe/Tiraspol", "Europe/Chisinau"},
        {"GB", "Europe/London"},
        {"GB-Eire", "Europe/London"},
        {"GMT+0", "Etc/GMT"},
        {"GMT-0", "Etc/GMT"},
        {"GMT0", "Etc/GMT"},
        {"Greenwich", "Etc/GMT"},
        {"Hongkong", "Asia/Hong_Kong"},
        {"Iceland", "Atlantic/Reykjavik"},
        {"Iran", "Asia/Tehran"},
        {"Israel", "Asia/Jerusalem"},
        {"Jamaica", "America/Jamaica"},
        {"Japan", "Asia/Tokyo"},
        {"Kwajalein", "Pacific/Kwajalein"},
        {"Libya", "Africa/Tripoli"},
        {"Mexico/BajaNorte", "America/Tijuana"},
        {"Mexico/BajaSur", "America/Mazatlan"},
        {"Mexico/General", "America/Mexico_City"},
        {"NZ", "Pacific/Auckland"},
        {"NZ-CHAT", "Pacific/Chatham"},
        {"Navajo", "America/Denver"},
        {"PRC", "Asia/Shanghai"},
        {"Pacific/Samoa", "Pacific/Pago_Pago"},
        {"Pacific/Yap", "Pacific/Chuuk"},
        {"Pacific/Truk", "Pacific/Chuuk"},
        {"Pacific/Ponape", "Pacific/Pohnpei"},
        {"Poland", "Europe/Warsaw"},
        {"Portugal", "Europe/Lisbon"},
        {"ROC", "Asia/Taipei"},
        {"ROK", "Asia/Seoul"},
        {"Singapore", "Asia/Singapore"},
        {"Turkey", "Europe/Istanbul"},
        {"UCT", "Etc/UCT"},
        {"US/Alaska", "America/Anchorage"},
        {"US/Aleutian", "America/Adak"},
        {"US/Arizona", "America/Phoenix"},
        {"US/Central", "America/Chicago"},
        {"US/East-Indiana", "America/Indiana/Indianapolis"},
        {"US/Eastern", "America/New_York"},
        {"US/Hawaii", "Pacific/Honolulu"},
        {"US/Indiana-Starke", "America/Indiana/Knox"},
        {"US/Michigan", "America/Detroit"},
        {"US/Mountain", "America/Denver"},
        {"US/Pacific", "America/Los_Angeles"},
        {"US/Samoa", "Pacific/Pago_Pago"},
        {"UTC", "Etc/UTC"},
        {"Universal", "Etc/UTC"},
        {"W-SU", "Europe/Moscow"},
        {"Zulu", "Etc/UTC"},
    };

    for (auto const& data : link_data) {
      map_[data.old_code] = map_[data.new_code];
    }
  }

 private:
  struct CompareCStrings {
    bool operator()(const char* str1, const char* str2) const {
      return strcmp(str1, str2) < 0;
    }
  };
  std::map<const char*, const char*, CompareCStrings> map_;
};

}  // namespace

std::string CountryCodeForCurrentTimezone() {
  base::FilePath target;
  base::FilePath timezone_path(kTimeZoneFilePath);
  while (base::ReadSymbolicLink(timezone_path, &target)) {
    timezone_path = base::FilePath(target);
  }
  if (timezone_path.value().compare(0, strlen(kZoneInfoFilePath),
                                    kZoneInfoFilePath)) {
    LOGF(WARNING) << "Timezone is unknown. Anti banding may be broken.";
    return std::string();
  }
  std::string olson_code = timezone_path.value();
  olson_code.replace(0, strlen(kZoneInfoFilePath), "");
  DLOGF(INFO) << "Timezone: " << olson_code;
  return TimezoneMap::GetInstance()->CountryCodeForTimezone(olson_code);
}

std::optional<v4l2_power_line_frequency> GetPowerLineFrequencyForLocation() {
  const std::string current_country = CountryCodeForCurrentTimezone();
  if (current_country.empty()) {
    return std::nullopt;
  }
  DLOGF(INFO) << "Country: " << current_country;
  // Sorted out list of countries with 60Hz power line frequency, from
  // http://en.wikipedia.org/wiki/Mains_electricity_by_country
  const char* countries_using_60Hz[] = {
      "AI", "AO", "AS", "AW", "AZ", "BM", "BR", "BS", "BZ", "CA", "CO",
      "CR", "CU", "DO", "EC", "FM", "GT", "GU", "GY", "HN", "HT", "JP",
      "KN", "KR", "KY", "MS", "MX", "NI", "PA", "PE", "PF", "PH", "PR",
      "PW", "SA", "SR", "SV", "TT", "TW", "UM", "US", "VG", "VI", "VE"};
  const char** countries_using_60Hz_end =
      countries_using_60Hz + std::size(countries_using_60Hz);
  if (std::find(countries_using_60Hz, countries_using_60Hz_end,
                current_country) == countries_using_60Hz_end) {
    return V4L2_CID_POWER_LINE_FREQUENCY_50HZ;
  }
  return V4L2_CID_POWER_LINE_FREQUENCY_60HZ;
}

}  // namespace cros
