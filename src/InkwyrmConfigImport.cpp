#include "InkwyrmConfigImport.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "JsonSettingsIO.h"
#include "OpdsServerStore.h"
#include "WifiCredentialStore.h"
#include "KOReaderCredentialStore.h"
#include "util/TimeZoneRegistry.h"

namespace {
constexpr char CONFIG_PATH[] = "/inkwyrm.conf";
constexpr char SCRUB_MARKER_PATH[] = "/inkwyrm.scrub.pending";
constexpr char SCRUB_NOTE[] = "# (imported & cleared)";

struct ParsedLine {
  enum class Type {
    Blank,
    Comment,
    Section,
    KeyValue,
    Other,
  };

  Type type = Type::Other;
  std::string raw;
  std::string section;
  std::string key;
  std::string value;
  bool secret = false;
};

struct ParsedConfig {
  bool keepSecretsOnSd = false;
  bool keepSecretsExplicit = false;
  bool hasImportedSecrets = false;
  std::string wifiSsid;
  std::string wifiPassword;
  bool hasWifiSsid = false;
  bool hasWifiPassword = false;
  std::string syncUrl;
  std::string syncUser;
  std::string syncPassword;
  bool hasSyncUrl = false;
  bool hasSyncUser = false;
  bool hasSyncPassword = false;
  uint8_t syncMatch = 0;  // 0=filename, 1=binary
  bool hasSyncMatch = false;
  std::string opdsUrl;
  std::string opdsUser;
  std::string opdsPassword;
  bool hasOpdsUrl = false;
  bool hasOpdsUser = false;
  bool hasOpdsPassword = false;
  uint8_t readerFontFamily = CrossPointSettings::NOTOSERIF;
  uint8_t readerFontSize = CrossPointSettings::MEDIUM;
  uint8_t readerLineSpacing = CrossPointSettings::NORMAL;
  bool hasReaderFontFamily = false;
  bool hasReaderFontSize = false;
  bool hasReaderLineSpacing = false;
  uint8_t timeZonePreset = TimeZoneRegistry::DEFAULT_TIME_ZONE_INDEX;
  bool hasTimeZonePreset = false;
  std::vector<ParsedLine> lines;
};

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string lower(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string normalizeToken(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch)) {
      out.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return out;
}

bool parseBool(const std::string& value, bool& out) {
  const std::string token = normalizeToken(value);
  if (token == "1" || token == "true" || token == "yes" || token == "on") {
    out = true;
    return true;
  }
  if (token == "0" || token == "false" || token == "no" || token == "off") {
    out = false;
    return true;
  }
  return false;
}

bool parseUint8Value(const std::string& value, uint8_t& out) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (!end || *end != '\0' || parsed < 0 || parsed > 255) {
    return false;
  }
  out = static_cast<uint8_t>(parsed);
  return true;
}

bool parseFontFamily(const std::string& value, uint8_t& out) {
  // 1.4.1 base has only NOTOSERIF/NOTOSANS built in (master's BOOKERLY/LEXEND
  // shipped with the old SD-font offload). Accept master's config vocabulary
  // and map to the nearest 1.4.1 family: serif names -> NOTOSERIF, sans ->
  // NOTOSANS. Revisit when the SD-font offload phase lands.
  const std::string token = normalizeToken(value);
  if (token == "0" || token == "bookerly" || token == "notoserif" || token == "serif") {
    out = CrossPointSettings::NOTOSERIF;
    return true;
  }
  if (token == "1" || token == "notosans" || token == "noto" || token == "lexend" || token == "sans") {
    out = CrossPointSettings::NOTOSANS;
    return true;
  }
  return false;
}

bool parseLineSpacing(const std::string& value, uint8_t& out) {
  const std::string token = normalizeToken(value);
  if (token == "0" || token == "tight" || token == "compact") {
    out = CrossPointSettings::TIGHT;
    return true;
  }
  if (token == "1" || token == "normal" || token == "regular") {
    out = CrossPointSettings::NORMAL;
    return true;
  }
  if (token == "2" || token == "wide" || token == "loose") {
    out = CrossPointSettings::WIDE;
    return true;
  }
  return false;
}

bool parseFontSize(const std::string& value, uint8_t& out) {
  const std::string token = normalizeToken(value);
  // No X_SMALL on the 1.4.1 base; accept the token, clamp to SMALL.
  if (token == "0" || token == "xsmall" || token == "xs" || token == "1" || token == "small" || token == "sm") {
    out = CrossPointSettings::SMALL;
    return true;
  }
  if (token == "2" || token == "medium" || token == "md") {
    out = CrossPointSettings::MEDIUM;
    return true;
  }
  if (token == "3" || token == "large" || token == "lg") {
    out = CrossPointSettings::LARGE;
    return true;
  }
  if (token == "4" || token == "extralarge" || token == "xl") {
    out = CrossPointSettings::EXTRA_LARGE;
    return true;
  }
  return false;
}

bool parseTimeZonePreset(const std::string& value, uint8_t& out) {
  const std::string trimmed = trim(value);
  if (trimmed.empty()) {
    return false;
  }

  uint8_t numeric = 0;
  if (parseUint8Value(trimmed, numeric)) {
    out = TimeZoneRegistry::clampPresetIndex(numeric);
    return true;
  }

  const std::string needle = normalizeToken(trimmed);
  const size_t count = TimeZoneRegistry::getPresetCount();
  for (size_t i = 0; i < count; ++i) {
    const char* label = TimeZoneRegistry::getPresetLabel(static_cast<uint8_t>(i));
    const char* posix = TimeZoneRegistry::getPresetPosixTz(static_cast<uint8_t>(i));
    const std::string labelToken = normalizeToken(label ? label : "");
    const std::string posixToken = normalizeToken(posix ? posix : "");
    if (needle == labelToken || needle == posixToken) {
      out = static_cast<uint8_t>(i);
      return true;
    }

    std::string prefix = label ? std::string(label) : "";
    const size_t cut = prefix.find_first_of(" (");
    if (cut != std::string::npos) {
      prefix.resize(cut);
      if (needle == normalizeToken(prefix)) {
        out = static_cast<uint8_t>(i);
        return true;
      }
    }
  }
  return false;
}

bool isSecretKey(const std::string& section, const std::string& key) {
  return (section == "wifi" && key == "password") || (section == "sync" && key == "kosync_password") ||
         (section == "opds" && key == "password");
}

bool parseKeyValueLine(const std::string& raw, const std::string& currentSection, ParsedConfig& config,
                       ParsedLine& outLine, const size_t lineNumber) {
  const size_t equalsPos = raw.find('=');
  if (equalsPos == std::string::npos) {
    outLine.type = ParsedLine::Type::Other;
    LOG_DBG("CFG", "Ignoring malformed config line %zu: missing '='", lineNumber);
    return false;
  }

  outLine.type = ParsedLine::Type::KeyValue;
  outLine.section = lower(trim(currentSection));
  outLine.key = lower(trim(raw.substr(0, equalsPos)));
  outLine.value = trim(raw.substr(equalsPos + 1));
  outLine.secret = isSecretKey(outLine.section, outLine.key);
  return true;
}

void applyReaderSetting(ParsedConfig& config, const std::string& key, const std::string& value, const size_t lineNumber) {
  if (key == "font" || key == "font_family") {
    if (parseFontFamily(value, config.readerFontFamily)) {
      config.hasReaderFontFamily = true;
    } else {
      LOG_DBG("CFG", "Ignoring invalid reader font family on line %zu", lineNumber);
    }
    return;
  }
  if (key == "font_size") {
    if (parseFontSize(value, config.readerFontSize)) {
      config.hasReaderFontSize = true;
    } else {
      LOG_DBG("CFG", "Ignoring invalid reader font size on line %zu", lineNumber);
    }
    return;
  }
  if (key == "line_height" || key == "line_spacing") {
    if (parseLineSpacing(value, config.readerLineSpacing)) {
      config.hasReaderLineSpacing = true;
    } else {
      LOG_DBG("CFG", "Ignoring invalid reader line height on line %zu", lineNumber);
    }
    return;
  }
  LOG_DBG("CFG", "Ignoring unknown reader key '%s' on line %zu", key.c_str(), lineNumber);
}

void applyKeyValue(ParsedConfig& config, const ParsedLine& line, const size_t lineNumber) {
  const std::string& section = line.section;
  const std::string& key = line.key;
  const std::string& value = line.value;

  if (section == "device") {
    if (key == "keep_secrets_on_sd") {
      bool keepSecrets = false;
      if (parseBool(value, keepSecrets)) {
        config.keepSecretsOnSd = keepSecrets;
        config.keepSecretsExplicit = true;
      } else {
        LOG_DBG("CFG", "Ignoring invalid keep_secrets_on_sd on line %zu", lineNumber);
      }
      return;
    }
    if (key == "timezone") {
      if (parseTimeZonePreset(value, config.timeZonePreset)) {
        config.hasTimeZonePreset = true;
      } else {
        LOG_DBG("CFG", "Ignoring invalid timezone on line %zu", lineNumber);
      }
      return;
    }
    LOG_DBG("CFG", "Ignoring unknown device key '%s' on line %zu", key.c_str(), lineNumber);
    return;
  }

  if (section == "wifi") {
    if (key == "ssid") {
      config.wifiSsid = value;
      config.hasWifiSsid = true;
      return;
    }
    if (key == "password") {
      if (value.empty()) {
        return;
      }
      config.wifiPassword = value;
      config.hasWifiPassword = true;
      config.hasImportedSecrets = true;
      return;
    }
    LOG_DBG("CFG", "Ignoring unknown wifi key '%s' on line %zu", key.c_str(), lineNumber);
    return;
  }

  if (section == "sync") {
    if (key == "kosync_url") {
      config.syncUrl = value;
      config.hasSyncUrl = true;
      return;
    }
    if (key == "kosync_user") {
      config.syncUser = value;
      config.hasSyncUser = true;
      return;
    }
    if (key == "kosync_password") {
      if (value.empty()) {
        return;
      }
      config.syncPassword = value;
      config.hasSyncPassword = true;
      config.hasImportedSecrets = true;
      return;
    }
    if (key == "kosync_match") {
      // Cross-device document matching. "binary" = partial-MD5 of file content
      // (works no matter what each device names the file, as long as the file
      // bytes are identical); "filename" = md5 of the filename (requires the
      // same filename on every device). Absent = leave the current setting.
      const std::string token = normalizeToken(value);
      if (token == "1" || token == "binary" || token == "content") {
        config.syncMatch = 1;
        config.hasSyncMatch = true;
      } else if (token == "0" || token == "filename" || token == "name") {
        config.syncMatch = 0;
        config.hasSyncMatch = true;
      }
      return;
    }
    LOG_DBG("CFG", "Ignoring unknown sync key '%s' on line %zu", key.c_str(), lineNumber);
    return;
  }

  if (section == "opds") {
    if (key == "url") {
      config.opdsUrl = value;
      config.hasOpdsUrl = true;
      return;
    }
    if (key == "user") {
      config.opdsUser = value;
      config.hasOpdsUser = true;
      return;
    }
    if (key == "password") {
      if (value.empty()) {
        return;
      }
      config.opdsPassword = value;
      config.hasOpdsPassword = true;
      config.hasImportedSecrets = true;
      return;
    }
    LOG_DBG("CFG", "Ignoring unknown opds key '%s' on line %zu", key.c_str(), lineNumber);
    return;
  }

  if (section == "reader") {
    applyReaderSetting(config, key, value, lineNumber);
    return;
  }

  if (!section.empty()) {
    LOG_DBG("CFG", "Ignoring unknown section '%s' on line %zu", section.c_str(), lineNumber);
  }
}

ParsedConfig parseConfig(const std::string& rawConfig) {
  ParsedConfig config;
  size_t start = 0;
  std::string currentSection;
  size_t lineNumber = 0;

  while (start <= rawConfig.size()) {
    // FIX #2: Reset watchdog EVERY iteration so long configs can't block >100ms
    esp_task_wdt_reset();

    const size_t end = rawConfig.find('\n', start);
    std::string rawLine = rawConfig.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!rawLine.empty() && rawLine.back() == '\r') {
      rawLine.pop_back();
    }
    ++lineNumber;

    ParsedLine line;
    line.raw = rawLine;
    const std::string trimmed = trim(rawLine);
    if (trimmed.empty()) {
      line.type = ParsedLine::Type::Blank;
    } else if (trimmed[0] == '#' || trimmed[0] == ';') {
      line.type = ParsedLine::Type::Comment;
    } else if (trimmed.front() == '[' && trimmed.back() == ']') {
      line.type = ParsedLine::Type::Section;
      currentSection = lower(trim(trimmed.substr(1, trimmed.size() - 2)));
      line.section = currentSection;
    } else if (parseKeyValueLine(rawLine, currentSection, config, line, lineNumber)) {
      applyKeyValue(config, line, lineNumber);
    }

    config.lines.push_back(std::move(line));

    // FIX #2: vTaskDelay every few lines (not just every 10)
    if (lineNumber % 5 == 0) {
      vTaskDelay(1);
    }

    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  return config;
}

bool shouldTreatAsScrubbedSecret(const ParsedLine& line, const ParsedConfig& config) {
  if (!line.secret || line.value.empty()) {
    return false;
  }
  if (config.keepSecretsOnSd) {
    return false;
  }
  return true;
}

std::string buildScrubbedConfig(const ParsedConfig& config) {
  std::string out;
  out.reserve(64 + config.lines.size() * 32);
  for (size_t i = 0; i < config.lines.size(); ++i) {
    const ParsedLine& line = config.lines[i];
    if (line.type == ParsedLine::Type::KeyValue && shouldTreatAsScrubbedSecret(line, config)) {
      out += line.key;
      out += " =\n";
      out += SCRUB_NOTE;
      out += "\n";
      continue;
    }
    out += line.raw;
    out += "\n";
  }
  return out;
}

void applyWifi(const ParsedConfig& config) {
  if (!config.hasWifiSsid && !config.hasWifiPassword) {
    return;
  }

  WIFI_STORE.loadFromFile();
  vTaskDelay(1);
  esp_task_wdt_reset();

  if (!config.wifiSsid.empty()) {
    if (config.hasWifiPassword) {
      if (WIFI_STORE.addCredential(config.wifiSsid, config.wifiPassword)) {
        LOG_INF("CFG", "Applied WiFi SSID from /inkwyrm.conf");
      } else {
        LOG_ERR("CFG", "Failed to persist WiFi credential from /inkwyrm.conf");
      }
      vTaskDelay(1);
      esp_task_wdt_reset();
      return;
    }

    // Scrubbed configs leave the password blank; keep any existing saved
    // credential instead of downgrading the network to an empty password.
    if (WIFI_STORE.hasSavedCredential(config.wifiSsid)) {
      LOG_INF("CFG", "Kept existing WiFi credential for %s from /inkwyrm.conf", config.wifiSsid.c_str());
      return;
    }

    if (WIFI_STORE.addCredential(config.wifiSsid, std::string())) {
      LOG_INF("CFG", "Applied open WiFi SSID from /inkwyrm.conf");
    } else {
      LOG_ERR("CFG", "Failed to persist open WiFi credential from /inkwyrm.conf");
    }
    vTaskDelay(1);
    esp_task_wdt_reset();
  } else if (config.hasWifiPassword && !config.wifiPassword.empty()) {
    LOG_DBG("CFG", "Ignoring WiFi password without SSID");
  }
}

void applyKoReaderSync(const ParsedConfig& config) {
  if (!config.hasSyncUrl && !config.hasSyncUser && !config.hasSyncPassword && !config.hasSyncMatch) {
    return;
  }

  KOREADER_STORE.loadFromFile();
  vTaskDelay(1);
  esp_task_wdt_reset();

  if (config.hasSyncUrl) {
    KOREADER_STORE.setServerUrl(config.syncUrl);
  }
  if (config.hasSyncUser || config.hasSyncPassword) {
    KOREADER_STORE.setCredentials(config.hasSyncUser ? config.syncUser : KOREADER_STORE.getUsername(),
                                  config.hasSyncPassword ? config.syncPassword : KOREADER_STORE.getPassword());
  }
  if (config.hasSyncMatch) {
    KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(config.syncMatch));
  }
  if (!KOREADER_STORE.saveToFile()) {
    LOG_ERR("CFG", "Failed to persist KOReader Sync settings from /inkwyrm.conf");
    return;
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  LOG_INF("CFG", "Applied KOReader Sync settings from /inkwyrm.conf");
}

void applyOpds(const ParsedConfig& config) {
  if (!config.hasOpdsUrl && !config.hasOpdsUser && !config.hasOpdsPassword) {
    return;
  }

  OPDS_STORE.loadFromFile();
  vTaskDelay(1);
  esp_task_wdt_reset();

  OpdsServer server;
  bool hasExisting = OPDS_STORE.getCount() > 0;
  if (hasExisting) {
    const auto* existing = OPDS_STORE.getServer(0);
    if (existing) {
      server = *existing;
    }
  }

  if (config.hasOpdsUrl) {
    server.url = config.opdsUrl;
  }
  if (config.hasOpdsUser) {
    server.username = config.opdsUser;
  }
  if (config.hasOpdsPassword) {
    server.password = config.opdsPassword;
  }
  if (server.name.empty()) {
    server.name = "OPDS Server";
  }

  bool saved = false;
  if (hasExisting) {
    saved = OPDS_STORE.updateServer(0, server);
  } else {
    saved = OPDS_STORE.addServer(server);
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  if (!saved) {
    LOG_ERR("CFG", "Failed to persist OPDS settings from /inkwyrm.conf");
    return;
  }

  LOG_INF("CFG", "Applied OPDS settings from /inkwyrm.conf");
}

void applyReaderAndDevice(const ParsedConfig& config) {
  bool settingsChanged = false;
  if (config.hasReaderFontFamily) {
    SETTINGS.fontFamily = config.readerFontFamily;
    settingsChanged = true;
  }
  if (config.hasReaderFontSize) {
    SETTINGS.fontSize = config.readerFontSize;
    settingsChanged = true;
  }
  if (config.hasReaderLineSpacing) {
    SETTINGS.lineSpacing = config.readerLineSpacing;
    settingsChanged = true;
  }
  if (config.hasTimeZonePreset) {
    SETTINGS.timeZonePreset = TimeZoneRegistry::clampPresetIndex(config.timeZonePreset);
    settingsChanged = true;
  }

  if (!settingsChanged) {
    return;
  }

  if (!SETTINGS.saveToFile()) {
    LOG_ERR("CFG", "Failed to persist reader/device settings from /inkwyrm.conf");
    return;
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  LOG_INF("CFG", "Applied reader/device settings from /inkwyrm.conf");
}

bool performResumableScrub(const ParsedConfig& config) {
  // Step 1: Write the scrub marker to indicate scrub in progress
  String markerContent = "1";
  if (!Storage.writeFile(SCRUB_MARKER_PATH, markerContent)) {
    LOG_ERR("CFG", "Failed to write scrub marker");
    return false;
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  // Step 2: Build the scrubbed config
  const std::string scrubbed = buildScrubbedConfig(config);
  vTaskDelay(1);
  esp_task_wdt_reset();

  // Step 3: Write the scrubbed config
  String scrubbedFile = scrubbed.c_str();
  if (!Storage.writeFile(CONFIG_PATH, scrubbedFile)) {
    LOG_ERR("CFG", "Failed to scrub secrets from /inkwyrm.conf");
    // Leave marker in place so we can retry on next boot
    return false;
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  // Step 4: Delete the marker on success
  if (Storage.exists(SCRUB_MARKER_PATH)) {
    Storage.remove(SCRUB_MARKER_PATH);
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  LOG_INF("CFG", "Scrubbed imported secrets from /inkwyrm.conf");
  return true;
}

bool completePendingScrub() {
  // FIX #1: Bound attempts to prevent infinite loop if marker is corrupt or SD ops keep failing
  constexpr int MAX_SCRUB_ATTEMPTS = 3;
  static int attemptCount = 0;

  // Check if there's a pending scrub from a previous interrupted boot
  esp_task_wdt_reset();  // FIX #3: Reset before exists() check (it does SD I/O)
  if (!Storage.exists(SCRUB_MARKER_PATH)) {
    attemptCount = 0;  // Reset counter when no pending scrub
    return true;
  }

  attemptCount++;
  if (attemptCount > MAX_SCRUB_ATTEMPTS) {
    LOG_ERR("CFG", "Pending scrub exhausted %d attempts; removing marker and giving up", MAX_SCRUB_ATTEMPTS);
    Storage.remove(SCRUB_MARKER_PATH);
    attemptCount = 0;
    return true;  // Give up gracefully, don't hang boot
  }

  LOG_INF("CFG", "Resuming interrupted credential scrub (attempt %d/%d)", attemptCount, MAX_SCRUB_ATTEMPTS);

  // Re-read and parse the config
  String raw = Storage.readFile(CONFIG_PATH);
  vTaskDelay(1);
  esp_task_wdt_reset();

  if (raw.isEmpty()) {
    // Config is empty or missing, just remove the marker
    Storage.remove(SCRUB_MARKER_PATH);
    attemptCount = 0;
    return true;
  }

  // Parse and perform the scrub
  ParsedConfig config = parseConfig(raw.c_str());
  vTaskDelay(1);
  esp_task_wdt_reset();

  // If secrets should be kept, just remove the marker
  if (config.keepSecretsOnSd) {
    Storage.remove(SCRUB_MARKER_PATH);
    attemptCount = 0;
    return true;
  }

  // Build and write the scrubbed config
  const std::string scrubbed = buildScrubbedConfig(config);
  String scrubbedFile = scrubbed.c_str();
  if (!Storage.writeFile(CONFIG_PATH, scrubbedFile)) {
    LOG_ERR("CFG", "Failed to complete pending scrub (attempt %d/%d)", attemptCount, MAX_SCRUB_ATTEMPTS);
    return false;  // Will retry on next boot if under max attempts
  }
  vTaskDelay(1);
  esp_task_wdt_reset();

  // Remove the marker
  Storage.remove(SCRUB_MARKER_PATH);
  vTaskDelay(1);
  esp_task_wdt_reset();

  LOG_INF("CFG", "Completed pending credential scrub");
  attemptCount = 0;  // Reset on success
  return true;
}

// FIX #5: Defer scrub write to after first render (off boot-critical window)
struct DeferredScrub {
  bool needed = false;
  ParsedConfig config;
};

static DeferredScrub deferredScrub;

}  // namespace

bool InkwyrmConfigImport::applyFromSdRoot() {
  // First, complete any pending scrub from a previous interrupted boot
  if (!completePendingScrub()) {
    LOG_ERR("CFG", "Failed to complete pending scrub, will retry on next boot");
    // Continue anyway to apply current config
  }

  if (!Storage.exists(CONFIG_PATH)) {
    LOG_DBG("CFG", "No /inkwyrm.conf found");
    return false;
  }

  // Read config with yield after
  String raw = Storage.readFile(CONFIG_PATH);
  vTaskDelay(1);
  esp_task_wdt_reset();

  if (raw.isEmpty()) {
    LOG_DBG("CFG", "/inkwyrm.conf is empty");
    return true;
  }

  // Parse config (yields are inside parseConfig)
  ParsedConfig config = parseConfig(raw.c_str());
  vTaskDelay(1);
  esp_task_wdt_reset();

  // Apply settings in-memory (yields are inside each apply function)
  applyWifi(config);
  applyKoReaderSync(config);
  applyOpds(config);
  applyReaderAndDevice(config);

  // FIX #5: Defer credential scrub write to after first render
  if (config.hasImportedSecrets && !config.keepSecretsOnSd) {
    deferredScrub.config = config;
    deferredScrub.needed = true;
    LOG_DBG("CFG", "Deferred credential scrub until after first render");
  } else if (config.keepSecretsExplicit) {
    LOG_INF("CFG", "Kept secrets on /inkwyrm.conf as requested");
  }

  return true;
}

void InkwyrmConfigImport::performDeferredScrub() {
  if (!deferredScrub.needed) {
    return;
  }

  LOG_DBG("CFG", "Performing deferred credential scrub");

  if (!performResumableScrub(deferredScrub.config)) {
    LOG_ERR("CFG", "Failed to perform deferred scrub");
  }

  deferredScrub.needed = false;
}
