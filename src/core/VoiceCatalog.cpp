#include "VoiceCatalog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace tts {

namespace {

// Ported from app.py KOKORO_LANG_MAP: two-letter voice-name prefix ->
// (locale label, espeak-ng voice for phonemization).
struct LangInfo {
    const char* locale;
    const char* espeakLang;
};

const std::unordered_map<char, LangInfo>& kokoroLangMap() {
    // espeakLang values are espeak-ng voice identifiers that actually exist
    // as files under espeak-ng-data/lang/ (espeak_SetVoiceByName matches by
    // identifier/filename, not by the "language" lines inside each file —
    // e.g. "en-gb" is *not* a valid identifier even though gmw/en declares
    // "language en-gb 2"; the GB voice file is named "en-GB-x-rp").
    static const std::unordered_map<char, LangInfo> m = {
        {'a', {"en-US", "en-us"}},
        {'b', {"en-GB", "en-gb-x-rp"}},
        {'e', {"es-ES", "es"}},
        {'f', {"fr-FR", "fr"}},
        {'h', {"hi-IN", "hi"}},
        {'i', {"it-IT", "it"}},
        {'j', {"ja-JP", "ja"}},
        {'p', {"pt-BR", "pt-BR"}},
        {'z', {"zh-CN", "cmn"}},
    };
    return m;
}

// Ported from app.py KOKORO_VOICE_NAMES.
const std::vector<std::string>& kokoroVoiceNames() {
    static const std::vector<std::string> names = {
        "af_alloy", "af_aoede", "af_bella", "af_heart", "af_jessica", "af_kore",
        "af_nicole", "af_nova", "af_river", "af_sarah", "af_sky",
        "am_adam", "am_echo", "am_eric", "am_fenrir", "am_liam", "am_michael",
        "am_onyx", "am_puck", "am_santa",
        "bf_alice", "bf_emma", "bf_isabella", "bf_lily",
        "bm_daniel", "bm_fable", "bm_george", "bm_lewis",
        "ef_dora", "em_alex", "em_santa",
        "ff_siwis",
        "hf_alpha", "hf_beta", "hm_omega", "hm_psi",
        "if_sara", "im_nicola",
        "jf_alpha", "jf_gongitsune", "jf_nezumi", "jf_tebukuro", "jm_kumo",
        "pf_dora", "pm_alex", "pm_santa",
        "zf_xiaobei", "zf_xiaoni", "zf_xiaoxiao", "zf_xiaoyi",
        "zm_yunjian", "zm_yunxi", "zm_yunxia", "zm_yunyang",
    };
    return names;
}

std::string titleCase(const std::string& s) {
    std::string out = s;
    bool startOfWord = true;
    for (char& c : out) {
        if (startOfWord && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        startOfWord = (c == '_' || c == ' ');
    }
    for (char& c : out) {
        if (c == '_') c = ' ';
    }
    return out;
}

} // namespace

const std::vector<VoiceEntry>& supertonicVoices() {
    static const std::vector<VoiceEntry> voices = [] {
        std::vector<VoiceEntry> v;
        for (const char* name : {"M1", "M2", "M3", "M4", "M5", "F1", "F2", "F3", "F4", "F5"}) {
            std::string gender = (name[0] == 'M') ? "Male" : "Female";
            v.push_back({name, std::string("Supertonic - ") + name + " (Multilingual, " + gender + ")", ""});
        }
        return v;
    }();
    return voices;
}

const std::vector<VoiceEntry>& kokoroVoices() {
    static const std::vector<VoiceEntry> voices = [] {
        std::vector<VoiceEntry> v;
        const auto& langMap = kokoroLangMap();
        for (const std::string& name : kokoroVoiceNames()) {
            char langKey = name[0];
            char genderKey = name[1];
            std::string person = name.substr(name.find('_') + 1);
            LangInfo info = {"en-US", "en-us"};
            auto it = langMap.find(langKey);
            if (it != langMap.end()) info = it->second;
            std::string gender = (genderKey == 'f') ? "Female" : "Male";
            v.push_back({name,
                          "Kokoro - " + titleCase(person) + " (" + info.locale + ", " + gender + ")",
                          info.espeakLang});
        }
        // German-retuned Kokoro fork (Godelaune/Kokoro-82M-ONNX-German-Martin),
        // served from a separate model file (models/kokoro_de_martin/).
        v.push_back({"martin", "Kokoro (German) - Martin (de-DE)", "de"});
        // German female voice exported from kikiri-tts/kikiri-german-victoria,
        // served from a separate model file (models/kokoro_de_victoria/).
        v.push_back({"victoria", "Kokoro (German) - Victoria (de-DE)", "de"});
        return v;
    }();
    return voices;
}

std::vector<VoiceEntry> discoverPiperVoices(const std::string& piperModelDir) {
    static const std::vector<VoiceEntry> kFallback = {
        {"de_DE-thorsten-high", "Piper - Thorsten High (de-DE, Male)", "de"},
        {"de_DE-kerstin-low", "Piper - Kerstin (de-DE, Female)", "de"},
        {"de_DE-eva_k-x_low", "Piper - Eva K (de-DE, Female)", "de"},
    };
    std::vector<VoiceEntry> voices;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(piperModelDir, ec) || !fs::is_directory(piperModelDir, ec)) {
        return kFallback;
    }
    for (const auto& entry : fs::directory_iterator(piperModelDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string path = entry.path().string();
        if (entry.path().extension() != ".json" || path.size() < 10 ||
            path.substr(path.size() - 10) != ".onnx.json") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        const std::string shortName = stem.substr(0, stem.size() - 5); // strip ".onnx"
        const std::string onnxPath = piperModelDir + "/" + shortName + ".onnx";
        if (!fs::exists(onnxPath, ec)) {
            continue;
        }
        std::string lang = "de";
        std::string displayName = shortName;
        std::ifstream in(path);
        if (in) {
            try {
                nlohmann::json j = nlohmann::json::parse(in);
                if (j.contains("language") && j["language"].is_object()) {
                    const auto& langObj = j["language"];
                    if (langObj.contains("code")) {
                        lang = langObj["code"].get<std::string>();
                    }
                    if (langObj.contains("name_english")) {
                        displayName = langObj["name_english"].get<std::string>();
                    }
                }
            } catch (...) {
            }
        }
        std::string espeakLang = lang;
        if (lang.find('_') != std::string::npos) {
            espeakLang = lang.substr(0, lang.find('_'));
        }
        voices.push_back({shortName, "Piper - " + displayName + " (" + lang + ")", espeakLang});
    }
    if (voices.empty()) {
        return kFallback;
    }
    std::sort(voices.begin(), voices.end(),
              [](const VoiceEntry& a, const VoiceEntry& b) { return a.friendlyName < b.friendlyName; });
    return voices;
}

namespace {
std::vector<VoiceEntry>& piperVoiceCache() {
    static std::vector<VoiceEntry> voices = {
        {"de_DE-thorsten-high", "Piper - Thorsten High (de-DE, Male)", "de"},
        {"de_DE-kerstin-low", "Piper - Kerstin (de-DE, Female)", "de"},
        {"de_DE-eva_k-x_low", "Piper - Eva K (de-DE, Female)", "de"},
    };
    return voices;
}
} // namespace

void refreshPiperVoices(const std::string& piperModelDir) {
    static std::string cachedDir;
    if (piperModelDir != cachedDir) {
        piperVoiceCache() = discoverPiperVoices(piperModelDir);
        cachedDir = piperModelDir;
    }
}

const std::vector<VoiceEntry>& piperVoices() { return piperVoiceCache(); }

const std::vector<VoiceEntry>& edgeTtsVoices() {
    // Full official Microsoft Edge neural voice catalog (322 voices, all locales).
    static const std::vector<VoiceEntry> voices = {
        {"af-ZA-AdriNeural", "Microsoft Edge - Adri (af-ZA, Female)", ""},
        {"af-ZA-WillemNeural", "Microsoft Edge - Willem (af-ZA, Male)", ""},
        {"sq-AL-AnilaNeural", "Microsoft Edge - Anila (sq-AL, Female)", ""},
        {"sq-AL-IlirNeural", "Microsoft Edge - Ilir (sq-AL, Male)", ""},
        {"am-ET-AmehaNeural", "Microsoft Edge - Ameha (am-ET, Male)", ""},
        {"am-ET-MekdesNeural", "Microsoft Edge - Mekdes (am-ET, Female)", ""},
        {"ar-DZ-AminaNeural", "Microsoft Edge - Amina (ar-DZ, Female)", ""},
        {"ar-DZ-IsmaelNeural", "Microsoft Edge - Ismael (ar-DZ, Male)", ""},
        {"ar-BH-AliNeural", "Microsoft Edge - Ali (ar-BH, Male)", ""},
        {"ar-BH-LailaNeural", "Microsoft Edge - Laila (ar-BH, Female)", ""},
        {"ar-EG-SalmaNeural", "Microsoft Edge - Salma (ar-EG, Female)", ""},
        {"ar-EG-ShakirNeural", "Microsoft Edge - Shakir (ar-EG, Male)", ""},
        {"ar-IQ-BasselNeural", "Microsoft Edge - Bassel (ar-IQ, Male)", ""},
        {"ar-IQ-RanaNeural", "Microsoft Edge - Rana (ar-IQ, Female)", ""},
        {"ar-JO-SanaNeural", "Microsoft Edge - Sana (ar-JO, Female)", ""},
        {"ar-JO-TaimNeural", "Microsoft Edge - Taim (ar-JO, Male)", ""},
        {"ar-KW-FahedNeural", "Microsoft Edge - Fahed (ar-KW, Male)", ""},
        {"ar-KW-NouraNeural", "Microsoft Edge - Noura (ar-KW, Female)", ""},
        {"ar-LB-LaylaNeural", "Microsoft Edge - Layla (ar-LB, Female)", ""},
        {"ar-LB-RamiNeural", "Microsoft Edge - Rami (ar-LB, Male)", ""},
        {"ar-LY-ImanNeural", "Microsoft Edge - Iman (ar-LY, Female)", ""},
        {"ar-LY-OmarNeural", "Microsoft Edge - Omar (ar-LY, Male)", ""},
        {"ar-MA-JamalNeural", "Microsoft Edge - Jamal (ar-MA, Male)", ""},
        {"ar-MA-MounaNeural", "Microsoft Edge - Mouna (ar-MA, Female)", ""},
        {"ar-OM-AbdullahNeural", "Microsoft Edge - Abdullah (ar-OM, Male)", ""},
        {"ar-OM-AyshaNeural", "Microsoft Edge - Aysha (ar-OM, Female)", ""},
        {"ar-QA-AmalNeural", "Microsoft Edge - Amal (ar-QA, Female)", ""},
        {"ar-QA-MoazNeural", "Microsoft Edge - Moaz (ar-QA, Male)", ""},
        {"ar-SA-HamedNeural", "Microsoft Edge - Hamed (ar-SA, Male)", ""},
        {"ar-SA-ZariyahNeural", "Microsoft Edge - Zariyah (ar-SA, Female)", ""},
        {"ar-SY-AmanyNeural", "Microsoft Edge - Amany (ar-SY, Female)", ""},
        {"ar-SY-LaithNeural", "Microsoft Edge - Laith (ar-SY, Male)", ""},
        {"ar-TN-HediNeural", "Microsoft Edge - Hedi (ar-TN, Male)", ""},
        {"ar-TN-ReemNeural", "Microsoft Edge - Reem (ar-TN, Female)", ""},
        {"ar-AE-FatimaNeural", "Microsoft Edge - Fatima (ar-AE, Female)", ""},
        {"ar-AE-HamdanNeural", "Microsoft Edge - Hamdan (ar-AE, Male)", ""},
        {"ar-YE-MaryamNeural", "Microsoft Edge - Maryam (ar-YE, Female)", ""},
        {"ar-YE-SalehNeural", "Microsoft Edge - Saleh (ar-YE, Male)", ""},
        {"az-AZ-BabekNeural", "Microsoft Edge - Babek (az-AZ, Male)", ""},
        {"az-AZ-BanuNeural", "Microsoft Edge - Banu (az-AZ, Female)", ""},
        {"bn-BD-NabanitaNeural", "Microsoft Edge - Nabanita (bn-BD, Female)", ""},
        {"bn-BD-PradeepNeural", "Microsoft Edge - Pradeep (bn-BD, Male)", ""},
        {"bn-IN-BashkarNeural", "Microsoft Edge - Bashkar (bn-IN, Male)", ""},
        {"bn-IN-TanishaaNeural", "Microsoft Edge - Tanishaa (bn-IN, Female)", ""},
        {"bs-BA-VesnaNeural", "Microsoft Edge - Vesna (bs-BA, Female)", ""},
        {"bs-BA-GoranNeural", "Microsoft Edge - Goran (bs-BA, Male)", ""},
        {"bg-BG-BorislavNeural", "Microsoft Edge - Borislav (bg-BG, Male)", ""},
        {"bg-BG-KalinaNeural", "Microsoft Edge - Kalina (bg-BG, Female)", ""},
        {"my-MM-NilarNeural", "Microsoft Edge - Nilar (my-MM, Female)", ""},
        {"my-MM-ThihaNeural", "Microsoft Edge - Thiha (my-MM, Male)", ""},
        {"ca-ES-EnricNeural", "Microsoft Edge - Enric (ca-ES, Male)", ""},
        {"ca-ES-JoanaNeural", "Microsoft Edge - Joana (ca-ES, Female)", ""},
        {"zh-HK-HiuGaaiNeural", "Microsoft Edge - HiuGaai (zh-HK, Female)", ""},
        {"zh-HK-HiuMaanNeural", "Microsoft Edge - HiuMaan (zh-HK, Female)", ""},
        {"zh-HK-WanLungNeural", "Microsoft Edge - WanLung (zh-HK, Male)", ""},
        {"zh-CN-XiaoxiaoNeural", "Microsoft Edge - Xiaoxiao (zh-CN, Female)", ""},
        {"zh-CN-XiaoyiNeural", "Microsoft Edge - Xiaoyi (zh-CN, Female)", ""},
        {"zh-CN-YunjianNeural", "Microsoft Edge - Yunjian (zh-CN, Male)", ""},
        {"zh-CN-YunxiNeural", "Microsoft Edge - Yunxi (zh-CN, Male)", ""},
        {"zh-CN-YunxiaNeural", "Microsoft Edge - Yunxia (zh-CN, Male)", ""},
        {"zh-CN-YunyangNeural", "Microsoft Edge - Yunyang (zh-CN, Male)", ""},
        {"zh-CN-liaoning-XiaobeiNeural", "Microsoft Edge - Xiaobei (zh-CN-liaoning, Female)", ""},
        {"zh-TW-HsiaoChenNeural", "Microsoft Edge - HsiaoChen (zh-TW, Female)", ""},
        {"zh-TW-YunJheNeural", "Microsoft Edge - YunJhe (zh-TW, Male)", ""},
        {"zh-TW-HsiaoYuNeural", "Microsoft Edge - HsiaoYu (zh-TW, Female)", ""},
        {"zh-CN-shaanxi-XiaoniNeural", "Microsoft Edge - Xiaoni (zh-CN-shaanxi, Female)", ""},
        {"hr-HR-GabrijelaNeural", "Microsoft Edge - Gabrijela (hr-HR, Female)", ""},
        {"hr-HR-SreckoNeural", "Microsoft Edge - Srecko (hr-HR, Male)", ""},
        {"cs-CZ-AntoninNeural", "Microsoft Edge - Antonin (cs-CZ, Male)", ""},
        {"cs-CZ-VlastaNeural", "Microsoft Edge - Vlasta (cs-CZ, Female)", ""},
        {"da-DK-ChristelNeural", "Microsoft Edge - Christel (da-DK, Female)", ""},
        {"da-DK-JeppeNeural", "Microsoft Edge - Jeppe (da-DK, Male)", ""},
        {"nl-BE-ArnaudNeural", "Microsoft Edge - Arnaud (nl-BE, Male)", ""},
        {"nl-BE-DenaNeural", "Microsoft Edge - Dena (nl-BE, Female)", ""},
        {"nl-NL-ColetteNeural", "Microsoft Edge - Colette (nl-NL, Female)", ""},
        {"nl-NL-FennaNeural", "Microsoft Edge - Fenna (nl-NL, Female)", ""},
        {"nl-NL-MaartenNeural", "Microsoft Edge - Maarten (nl-NL, Male)", ""},
        {"en-AU-WilliamMultilingualNeural", "Microsoft Edge - William (en-AU, Male, Multilingual)", ""},
        {"en-AU-NatashaNeural", "Microsoft Edge - Natasha (en-AU, Female)", ""},
        {"en-CA-ClaraNeural", "Microsoft Edge - Clara (en-CA, Female)", ""},
        {"en-CA-LiamNeural", "Microsoft Edge - Liam (en-CA, Male)", ""},
        {"en-HK-YanNeural", "Microsoft Edge - Yan (en-HK, Female)", ""},
        {"en-HK-SamNeural", "Microsoft Edge - Sam (en-HK, Male)", ""},
        {"en-IN-NeerjaExpressiveNeural", "Microsoft Edge - NeerjaExpressive (en-IN, Female)", ""},
        {"en-IN-NeerjaNeural", "Microsoft Edge - Neerja (en-IN, Female)", ""},
        {"en-IN-PrabhatNeural", "Microsoft Edge - Prabhat (en-IN, Male)", ""},
        {"en-IE-ConnorNeural", "Microsoft Edge - Connor (en-IE, Male)", ""},
        {"en-IE-EmilyNeural", "Microsoft Edge - Emily (en-IE, Female)", ""},
        {"en-KE-AsiliaNeural", "Microsoft Edge - Asilia (en-KE, Female)", ""},
        {"en-KE-ChilembaNeural", "Microsoft Edge - Chilemba (en-KE, Male)", ""},
        {"en-NZ-MitchellNeural", "Microsoft Edge - Mitchell (en-NZ, Male)", ""},
        {"en-NZ-MollyNeural", "Microsoft Edge - Molly (en-NZ, Female)", ""},
        {"en-NG-AbeoNeural", "Microsoft Edge - Abeo (en-NG, Male)", ""},
        {"en-NG-EzinneNeural", "Microsoft Edge - Ezinne (en-NG, Female)", ""},
        {"en-PH-JamesNeural", "Microsoft Edge - James (en-PH, Male)", ""},
        {"en-PH-RosaNeural", "Microsoft Edge - Rosa (en-PH, Female)", ""},
        {"en-US-AvaNeural", "Microsoft Edge - Ava (en-US, Female)", ""},
        {"en-US-AndrewNeural", "Microsoft Edge - Andrew (en-US, Male)", ""},
        {"en-US-EmmaNeural", "Microsoft Edge - Emma (en-US, Female)", ""},
        {"en-US-BrianNeural", "Microsoft Edge - Brian (en-US, Male)", ""},
        {"en-SG-LunaNeural", "Microsoft Edge - Luna (en-SG, Female)", ""},
        {"en-SG-WayneNeural", "Microsoft Edge - Wayne (en-SG, Male)", ""},
        {"en-ZA-LeahNeural", "Microsoft Edge - Leah (en-ZA, Female)", ""},
        {"en-ZA-LukeNeural", "Microsoft Edge - Luke (en-ZA, Male)", ""},
        {"en-TZ-ElimuNeural", "Microsoft Edge - Elimu (en-TZ, Male)", ""},
        {"en-TZ-ImaniNeural", "Microsoft Edge - Imani (en-TZ, Female)", ""},
        {"en-GB-LibbyNeural", "Microsoft Edge - Libby (en-GB, Female)", ""},
        {"en-GB-MaisieNeural", "Microsoft Edge - Maisie (en-GB, Female)", ""},
        {"en-GB-RyanNeural", "Microsoft Edge - Ryan (en-GB, Male)", ""},
        {"en-GB-SoniaNeural", "Microsoft Edge - Sonia (en-GB, Female)", ""},
        {"en-GB-ThomasNeural", "Microsoft Edge - Thomas (en-GB, Male)", ""},
        {"en-US-AnaNeural", "Microsoft Edge - Ana (en-US, Female)", ""},
        {"en-US-AndrewMultilingualNeural", "Microsoft Edge - Andrew (en-US, Male, Multilingual)", ""},
        {"en-US-AriaNeural", "Microsoft Edge - Aria (en-US, Female)", ""},
        {"en-US-AvaMultilingualNeural", "Microsoft Edge - Ava (en-US, Female, Multilingual)", ""},
        {"en-US-BrianMultilingualNeural", "Microsoft Edge - Brian (en-US, Male, Multilingual)", ""},
        {"en-US-ChristopherNeural", "Microsoft Edge - Christopher (en-US, Male)", ""},
        {"en-US-EmmaMultilingualNeural", "Microsoft Edge - Emma (en-US, Female, Multilingual)", ""},
        {"en-US-EricNeural", "Microsoft Edge - Eric (en-US, Male)", ""},
        {"en-US-GuyNeural", "Microsoft Edge - Guy (en-US, Male)", ""},
        {"en-US-JennyNeural", "Microsoft Edge - Jenny (en-US, Female)", ""},
        {"en-US-MichelleNeural", "Microsoft Edge - Michelle (en-US, Female)", ""},
        {"en-US-RogerNeural", "Microsoft Edge - Roger (en-US, Male)", ""},
        {"en-US-SteffanNeural", "Microsoft Edge - Steffan (en-US, Male)", ""},
        {"et-EE-AnuNeural", "Microsoft Edge - Anu (et-EE, Female)", ""},
        {"et-EE-KertNeural", "Microsoft Edge - Kert (et-EE, Male)", ""},
        {"fil-PH-AngeloNeural", "Microsoft Edge - Angelo (fil-PH, Male)", ""},
        {"fil-PH-BlessicaNeural", "Microsoft Edge - Blessica (fil-PH, Female)", ""},
        {"fi-FI-HarriNeural", "Microsoft Edge - Harri (fi-FI, Male)", ""},
        {"fi-FI-NooraNeural", "Microsoft Edge - Noora (fi-FI, Female)", ""},
        {"fr-BE-CharlineNeural", "Microsoft Edge - Charline (fr-BE, Female)", ""},
        {"fr-BE-GerardNeural", "Microsoft Edge - Gerard (fr-BE, Male)", ""},
        {"fr-CA-ThierryNeural", "Microsoft Edge - Thierry (fr-CA, Male)", ""},
        {"fr-CA-AntoineNeural", "Microsoft Edge - Antoine (fr-CA, Male)", ""},
        {"fr-CA-JeanNeural", "Microsoft Edge - Jean (fr-CA, Male)", ""},
        {"fr-CA-SylvieNeural", "Microsoft Edge - Sylvie (fr-CA, Female)", ""},
        {"fr-FR-VivienneMultilingualNeural", "Microsoft Edge - Vivienne (fr-FR, Female, Multilingual)", ""},
        {"fr-FR-RemyMultilingualNeural", "Microsoft Edge - Remy (fr-FR, Male, Multilingual)", ""},
        {"fr-FR-DeniseNeural", "Microsoft Edge - Denise (fr-FR, Female)", ""},
        {"fr-FR-EloiseNeural", "Microsoft Edge - Eloise (fr-FR, Female)", ""},
        {"fr-FR-HenriNeural", "Microsoft Edge - Henri (fr-FR, Male)", ""},
        {"fr-CH-ArianeNeural", "Microsoft Edge - Ariane (fr-CH, Female)", ""},
        {"fr-CH-FabriceNeural", "Microsoft Edge - Fabrice (fr-CH, Male)", ""},
        {"gl-ES-RoiNeural", "Microsoft Edge - Roi (gl-ES, Male)", ""},
        {"gl-ES-SabelaNeural", "Microsoft Edge - Sabela (gl-ES, Female)", ""},
        {"ka-GE-EkaNeural", "Microsoft Edge - Eka (ka-GE, Female)", ""},
        {"ka-GE-GiorgiNeural", "Microsoft Edge - Giorgi (ka-GE, Male)", ""},
        {"de-AT-IngridNeural", "Microsoft Edge - Ingrid (de-AT, Female)", ""},
        {"de-AT-JonasNeural", "Microsoft Edge - Jonas (de-AT, Male)", ""},
        {"de-DE-SeraphinaMultilingualNeural", "Microsoft Edge - Seraphina (de-DE, Female, Multilingual)", ""},
        {"de-DE-FlorianMultilingualNeural", "Microsoft Edge - Florian (de-DE, Male, Multilingual)", ""},
        {"de-DE-AmalaNeural", "Microsoft Edge - Amala (de-DE, Female)", ""},
        {"de-DE-ConradNeural", "Microsoft Edge - Conrad (de-DE, Male)", ""},
        {"de-DE-KatjaNeural", "Microsoft Edge - Katja (de-DE, Female)", ""},
        {"de-DE-KillianNeural", "Microsoft Edge - Killian (de-DE, Male)", ""},
        {"de-CH-JanNeural", "Microsoft Edge - Jan (de-CH, Male)", ""},
        {"de-CH-LeniNeural", "Microsoft Edge - Leni (de-CH, Female)", ""},
        {"el-GR-AthinaNeural", "Microsoft Edge - Athina (el-GR, Female)", ""},
        {"el-GR-NestorasNeural", "Microsoft Edge - Nestoras (el-GR, Male)", ""},
        {"gu-IN-DhwaniNeural", "Microsoft Edge - Dhwani (gu-IN, Female)", ""},
        {"gu-IN-NiranjanNeural", "Microsoft Edge - Niranjan (gu-IN, Male)", ""},
        {"he-IL-AvriNeural", "Microsoft Edge - Avri (he-IL, Male)", ""},
        {"he-IL-HilaNeural", "Microsoft Edge - Hila (he-IL, Female)", ""},
        {"hi-IN-MadhurNeural", "Microsoft Edge - Madhur (hi-IN, Male)", ""},
        {"hi-IN-SwaraNeural", "Microsoft Edge - Swara (hi-IN, Female)", ""},
        {"hu-HU-NoemiNeural", "Microsoft Edge - Noemi (hu-HU, Female)", ""},
        {"hu-HU-TamasNeural", "Microsoft Edge - Tamas (hu-HU, Male)", ""},
        {"is-IS-GudrunNeural", "Microsoft Edge - Gudrun (is-IS, Female)", ""},
        {"is-IS-GunnarNeural", "Microsoft Edge - Gunnar (is-IS, Male)", ""},
        {"id-ID-ArdiNeural", "Microsoft Edge - Ardi (id-ID, Male)", ""},
        {"id-ID-GadisNeural", "Microsoft Edge - Gadis (id-ID, Female)", ""},
        {"iu-Latn-CA-SiqiniqNeural", "Microsoft Edge - Siqiniq (iu-Latn-CA, Female)", ""},
        {"iu-Latn-CA-TaqqiqNeural", "Microsoft Edge - Taqqiq (iu-Latn-CA, Male)", ""},
        {"iu-Cans-CA-SiqiniqNeural", "Microsoft Edge - Siqiniq (iu-Cans-CA, Female)", ""},
        {"iu-Cans-CA-TaqqiqNeural", "Microsoft Edge - Taqqiq (iu-Cans-CA, Male)", ""},
        {"ga-IE-ColmNeural", "Microsoft Edge - Colm (ga-IE, Male)", ""},
        {"ga-IE-OrlaNeural", "Microsoft Edge - Orla (ga-IE, Female)", ""},
        {"it-IT-GiuseppeMultilingualNeural", "Microsoft Edge - Giuseppe (it-IT, Male, Multilingual)", ""},
        {"it-IT-DiegoNeural", "Microsoft Edge - Diego (it-IT, Male)", ""},
        {"it-IT-ElsaNeural", "Microsoft Edge - Elsa (it-IT, Female)", ""},
        {"it-IT-IsabellaNeural", "Microsoft Edge - Isabella (it-IT, Female)", ""},
        {"ja-JP-KeitaNeural", "Microsoft Edge - Keita (ja-JP, Male)", ""},
        {"ja-JP-NanamiNeural", "Microsoft Edge - Nanami (ja-JP, Female)", ""},
        {"jv-ID-DimasNeural", "Microsoft Edge - Dimas (jv-ID, Male)", ""},
        {"jv-ID-SitiNeural", "Microsoft Edge - Siti (jv-ID, Female)", ""},
        {"kn-IN-GaganNeural", "Microsoft Edge - Gagan (kn-IN, Male)", ""},
        {"kn-IN-SapnaNeural", "Microsoft Edge - Sapna (kn-IN, Female)", ""},
        {"kk-KZ-AigulNeural", "Microsoft Edge - Aigul (kk-KZ, Female)", ""},
        {"kk-KZ-DauletNeural", "Microsoft Edge - Daulet (kk-KZ, Male)", ""},
        {"km-KH-PisethNeural", "Microsoft Edge - Piseth (km-KH, Male)", ""},
        {"km-KH-SreymomNeural", "Microsoft Edge - Sreymom (km-KH, Female)", ""},
        {"ko-KR-HyunsuMultilingualNeural", "Microsoft Edge - Hyunsu (ko-KR, Male, Multilingual)", ""},
        {"ko-KR-InJoonNeural", "Microsoft Edge - InJoon (ko-KR, Male)", ""},
        {"ko-KR-SunHiNeural", "Microsoft Edge - SunHi (ko-KR, Female)", ""},
        {"lo-LA-ChanthavongNeural", "Microsoft Edge - Chanthavong (lo-LA, Male)", ""},
        {"lo-LA-KeomanyNeural", "Microsoft Edge - Keomany (lo-LA, Female)", ""},
        {"lv-LV-EveritaNeural", "Microsoft Edge - Everita (lv-LV, Female)", ""},
        {"lv-LV-NilsNeural", "Microsoft Edge - Nils (lv-LV, Male)", ""},
        {"lt-LT-LeonasNeural", "Microsoft Edge - Leonas (lt-LT, Male)", ""},
        {"lt-LT-OnaNeural", "Microsoft Edge - Ona (lt-LT, Female)", ""},
        {"mk-MK-AleksandarNeural", "Microsoft Edge - Aleksandar (mk-MK, Male)", ""},
        {"mk-MK-MarijaNeural", "Microsoft Edge - Marija (mk-MK, Female)", ""},
        {"ms-MY-OsmanNeural", "Microsoft Edge - Osman (ms-MY, Male)", ""},
        {"ms-MY-YasminNeural", "Microsoft Edge - Yasmin (ms-MY, Female)", ""},
        {"ml-IN-MidhunNeural", "Microsoft Edge - Midhun (ml-IN, Male)", ""},
        {"ml-IN-SobhanaNeural", "Microsoft Edge - Sobhana (ml-IN, Female)", ""},
        {"mt-MT-GraceNeural", "Microsoft Edge - Grace (mt-MT, Female)", ""},
        {"mt-MT-JosephNeural", "Microsoft Edge - Joseph (mt-MT, Male)", ""},
        {"mr-IN-AarohiNeural", "Microsoft Edge - Aarohi (mr-IN, Female)", ""},
        {"mr-IN-ManoharNeural", "Microsoft Edge - Manohar (mr-IN, Male)", ""},
        {"mn-MN-BataaNeural", "Microsoft Edge - Bataa (mn-MN, Male)", ""},
        {"mn-MN-YesuiNeural", "Microsoft Edge - Yesui (mn-MN, Female)", ""},
        {"ne-NP-HemkalaNeural", "Microsoft Edge - Hemkala (ne-NP, Female)", ""},
        {"ne-NP-SagarNeural", "Microsoft Edge - Sagar (ne-NP, Male)", ""},
        {"nb-NO-FinnNeural", "Microsoft Edge - Finn (nb-NO, Male)", ""},
        {"nb-NO-PernilleNeural", "Microsoft Edge - Pernille (nb-NO, Female)", ""},
        {"ps-AF-GulNawazNeural", "Microsoft Edge - GulNawaz (ps-AF, Male)", ""},
        {"ps-AF-LatifaNeural", "Microsoft Edge - Latifa (ps-AF, Female)", ""},
        {"fa-IR-DilaraNeural", "Microsoft Edge - Dilara (fa-IR, Female)", ""},
        {"fa-IR-FaridNeural", "Microsoft Edge - Farid (fa-IR, Male)", ""},
        {"pl-PL-MarekNeural", "Microsoft Edge - Marek (pl-PL, Male)", ""},
        {"pl-PL-ZofiaNeural", "Microsoft Edge - Zofia (pl-PL, Female)", ""},
        {"pt-BR-ThalitaMultilingualNeural", "Microsoft Edge - Thalita (pt-BR, Female, Multilingual)", ""},
        {"pt-BR-AntonioNeural", "Microsoft Edge - Antonio (pt-BR, Male)", ""},
        {"pt-BR-FranciscaNeural", "Microsoft Edge - Francisca (pt-BR, Female)", ""},
        {"pt-PT-DuarteNeural", "Microsoft Edge - Duarte (pt-PT, Male)", ""},
        {"pt-PT-RaquelNeural", "Microsoft Edge - Raquel (pt-PT, Female)", ""},
        {"ro-RO-AlinaNeural", "Microsoft Edge - Alina (ro-RO, Female)", ""},
        {"ro-RO-EmilNeural", "Microsoft Edge - Emil (ro-RO, Male)", ""},
        {"ru-RU-DmitryNeural", "Microsoft Edge - Dmitry (ru-RU, Male)", ""},
        {"ru-RU-SvetlanaNeural", "Microsoft Edge - Svetlana (ru-RU, Female)", ""},
        {"sr-RS-NicholasNeural", "Microsoft Edge - Nicholas (sr-RS, Male)", ""},
        {"sr-RS-SophieNeural", "Microsoft Edge - Sophie (sr-RS, Female)", ""},
        {"si-LK-SameeraNeural", "Microsoft Edge - Sameera (si-LK, Male)", ""},
        {"si-LK-ThiliniNeural", "Microsoft Edge - Thilini (si-LK, Female)", ""},
        {"sk-SK-LukasNeural", "Microsoft Edge - Lukas (sk-SK, Male)", ""},
        {"sk-SK-ViktoriaNeural", "Microsoft Edge - Viktoria (sk-SK, Female)", ""},
        {"sl-SI-PetraNeural", "Microsoft Edge - Petra (sl-SI, Female)", ""},
        {"sl-SI-RokNeural", "Microsoft Edge - Rok (sl-SI, Male)", ""},
        {"so-SO-MuuseNeural", "Microsoft Edge - Muuse (so-SO, Male)", ""},
        {"so-SO-UbaxNeural", "Microsoft Edge - Ubax (so-SO, Female)", ""},
        {"es-AR-ElenaNeural", "Microsoft Edge - Elena (es-AR, Female)", ""},
        {"es-AR-TomasNeural", "Microsoft Edge - Tomas (es-AR, Male)", ""},
        {"es-BO-MarceloNeural", "Microsoft Edge - Marcelo (es-BO, Male)", ""},
        {"es-BO-SofiaNeural", "Microsoft Edge - Sofia (es-BO, Female)", ""},
        {"es-CL-CatalinaNeural", "Microsoft Edge - Catalina (es-CL, Female)", ""},
        {"es-CL-LorenzoNeural", "Microsoft Edge - Lorenzo (es-CL, Male)", ""},
        {"es-CO-GonzaloNeural", "Microsoft Edge - Gonzalo (es-CO, Male)", ""},
        {"es-CO-SalomeNeural", "Microsoft Edge - Salome (es-CO, Female)", ""},
        {"es-ES-XimenaNeural", "Microsoft Edge - Ximena (es-ES, Female)", ""},
        {"es-CR-JuanNeural", "Microsoft Edge - Juan (es-CR, Male)", ""},
        {"es-CR-MariaNeural", "Microsoft Edge - Maria (es-CR, Female)", ""},
        {"es-CU-BelkysNeural", "Microsoft Edge - Belkys (es-CU, Female)", ""},
        {"es-CU-ManuelNeural", "Microsoft Edge - Manuel (es-CU, Male)", ""},
        {"es-DO-EmilioNeural", "Microsoft Edge - Emilio (es-DO, Male)", ""},
        {"es-DO-RamonaNeural", "Microsoft Edge - Ramona (es-DO, Female)", ""},
        {"es-EC-AndreaNeural", "Microsoft Edge - Andrea (es-EC, Female)", ""},
        {"es-EC-LuisNeural", "Microsoft Edge - Luis (es-EC, Male)", ""},
        {"es-SV-LorenaNeural", "Microsoft Edge - Lorena (es-SV, Female)", ""},
        {"es-SV-RodrigoNeural", "Microsoft Edge - Rodrigo (es-SV, Male)", ""},
        {"es-GQ-JavierNeural", "Microsoft Edge - Javier (es-GQ, Male)", ""},
        {"es-GQ-TeresaNeural", "Microsoft Edge - Teresa (es-GQ, Female)", ""},
        {"es-GT-AndresNeural", "Microsoft Edge - Andres (es-GT, Male)", ""},
        {"es-GT-MartaNeural", "Microsoft Edge - Marta (es-GT, Female)", ""},
        {"es-HN-CarlosNeural", "Microsoft Edge - Carlos (es-HN, Male)", ""},
        {"es-HN-KarlaNeural", "Microsoft Edge - Karla (es-HN, Female)", ""},
        {"es-MX-DaliaNeural", "Microsoft Edge - Dalia (es-MX, Female)", ""},
        {"es-MX-JorgeNeural", "Microsoft Edge - Jorge (es-MX, Male)", ""},
        {"es-NI-FedericoNeural", "Microsoft Edge - Federico (es-NI, Male)", ""},
        {"es-NI-YolandaNeural", "Microsoft Edge - Yolanda (es-NI, Female)", ""},
        {"es-PA-MargaritaNeural", "Microsoft Edge - Margarita (es-PA, Female)", ""},
        {"es-PA-RobertoNeural", "Microsoft Edge - Roberto (es-PA, Male)", ""},
        {"es-PY-MarioNeural", "Microsoft Edge - Mario (es-PY, Male)", ""},
        {"es-PY-TaniaNeural", "Microsoft Edge - Tania (es-PY, Female)", ""},
        {"es-PE-AlexNeural", "Microsoft Edge - Alex (es-PE, Male)", ""},
        {"es-PE-CamilaNeural", "Microsoft Edge - Camila (es-PE, Female)", ""},
        {"es-PR-KarinaNeural", "Microsoft Edge - Karina (es-PR, Female)", ""},
        {"es-PR-VictorNeural", "Microsoft Edge - Victor (es-PR, Male)", ""},
        {"es-ES-AlvaroNeural", "Microsoft Edge - Alvaro (es-ES, Male)", ""},
        {"es-ES-ElviraNeural", "Microsoft Edge - Elvira (es-ES, Female)", ""},
        {"es-US-AlonsoNeural", "Microsoft Edge - Alonso (es-US, Male)", ""},
        {"es-US-PalomaNeural", "Microsoft Edge - Paloma (es-US, Female)", ""},
        {"es-UY-MateoNeural", "Microsoft Edge - Mateo (es-UY, Male)", ""},
        {"es-UY-ValentinaNeural", "Microsoft Edge - Valentina (es-UY, Female)", ""},
        {"es-VE-PaolaNeural", "Microsoft Edge - Paola (es-VE, Female)", ""},
        {"es-VE-SebastianNeural", "Microsoft Edge - Sebastian (es-VE, Male)", ""},
        {"su-ID-JajangNeural", "Microsoft Edge - Jajang (su-ID, Male)", ""},
        {"su-ID-TutiNeural", "Microsoft Edge - Tuti (su-ID, Female)", ""},
        {"sw-KE-RafikiNeural", "Microsoft Edge - Rafiki (sw-KE, Male)", ""},
        {"sw-KE-ZuriNeural", "Microsoft Edge - Zuri (sw-KE, Female)", ""},
        {"sw-TZ-DaudiNeural", "Microsoft Edge - Daudi (sw-TZ, Male)", ""},
        {"sw-TZ-RehemaNeural", "Microsoft Edge - Rehema (sw-TZ, Female)", ""},
        {"sv-SE-MattiasNeural", "Microsoft Edge - Mattias (sv-SE, Male)", ""},
        {"sv-SE-SofieNeural", "Microsoft Edge - Sofie (sv-SE, Female)", ""},
        {"ta-IN-PallaviNeural", "Microsoft Edge - Pallavi (ta-IN, Female)", ""},
        {"ta-IN-ValluvarNeural", "Microsoft Edge - Valluvar (ta-IN, Male)", ""},
        {"ta-MY-KaniNeural", "Microsoft Edge - Kani (ta-MY, Female)", ""},
        {"ta-MY-SuryaNeural", "Microsoft Edge - Surya (ta-MY, Male)", ""},
        {"ta-SG-AnbuNeural", "Microsoft Edge - Anbu (ta-SG, Male)", ""},
        {"ta-SG-VenbaNeural", "Microsoft Edge - Venba (ta-SG, Female)", ""},
        {"ta-LK-KumarNeural", "Microsoft Edge - Kumar (ta-LK, Male)", ""},
        {"ta-LK-SaranyaNeural", "Microsoft Edge - Saranya (ta-LK, Female)", ""},
        {"te-IN-MohanNeural", "Microsoft Edge - Mohan (te-IN, Male)", ""},
        {"te-IN-ShrutiNeural", "Microsoft Edge - Shruti (te-IN, Female)", ""},
        {"th-TH-NiwatNeural", "Microsoft Edge - Niwat (th-TH, Male)", ""},
        {"th-TH-PremwadeeNeural", "Microsoft Edge - Premwadee (th-TH, Female)", ""},
        {"tr-TR-EmelNeural", "Microsoft Edge - Emel (tr-TR, Female)", ""},
        {"tr-TR-AhmetNeural", "Microsoft Edge - Ahmet (tr-TR, Male)", ""},
        {"uk-UA-OstapNeural", "Microsoft Edge - Ostap (uk-UA, Male)", ""},
        {"uk-UA-PolinaNeural", "Microsoft Edge - Polina (uk-UA, Female)", ""},
        {"ur-IN-GulNeural", "Microsoft Edge - Gul (ur-IN, Female)", ""},
        {"ur-IN-SalmanNeural", "Microsoft Edge - Salman (ur-IN, Male)", ""},
        {"ur-PK-AsadNeural", "Microsoft Edge - Asad (ur-PK, Male)", ""},
        {"ur-PK-UzmaNeural", "Microsoft Edge - Uzma (ur-PK, Female)", ""},
        {"uz-UZ-MadinaNeural", "Microsoft Edge - Madina (uz-UZ, Female)", ""},
        {"uz-UZ-SardorNeural", "Microsoft Edge - Sardor (uz-UZ, Male)", ""},
        {"vi-VN-HoaiMyNeural", "Microsoft Edge - HoaiMy (vi-VN, Female)", ""},
        {"vi-VN-NamMinhNeural", "Microsoft Edge - NamMinh (vi-VN, Male)", ""},
        {"cy-GB-AledNeural", "Microsoft Edge - Aled (cy-GB, Male)", ""},
        {"cy-GB-NiaNeural", "Microsoft Edge - Nia (cy-GB, Female)", ""},
        {"zu-ZA-ThandoNeural", "Microsoft Edge - Thando (zu-ZA, Female)", ""},
        {"zu-ZA-ThembaNeural", "Microsoft Edge - Themba (zu-ZA, Male)", ""},
    };
    return voices;
}

const std::vector<VoiceEntry>& fishSpeechVoices() {
    // Labels are overridden dynamically by updateFishVoiceComboLabels() in
    // MainWindow to show saved-state indicators; these are just the fallback defaults.
    static const std::vector<VoiceEntry> voices = {
        {"random", "Fish Audio S2 - Random Voice  (no reference)", ""},
        {"slot_1", "Fish Audio S2 - Voice Slot 1", ""},
        {"slot_2", "Fish Audio S2 - Voice Slot 2", ""},
        {"slot_3", "Fish Audio S2 - Voice Slot 3", ""},
    };
    return voices;
}

const std::vector<VoiceEntry>& voicesForProvider(Provider provider) {
    switch (provider) {
        case Provider::Kokoro:     return kokoroVoices();
        case Provider::Piper:      return piperVoices();
        case Provider::EdgeTts:    return edgeTtsVoices();
        case Provider::FishSpeech: return fishSpeechVoices();
        case Provider::Supertonic:
        default: return supertonicVoices();
    }
}

bool supportsVoiceMixing(Provider provider) {
    return provider == Provider::Supertonic || provider == Provider::Kokoro;
}

std::string voiceGender(const VoiceEntry& v) {
    // friendlyName ends with ", Male)" or ", Female)" (also "Male, Multilingual)").
    const std::string& f = v.friendlyName;
    if (f.find("Female") != std::string::npos) {
        return "Female";
    }
    if (f.find("Male") != std::string::npos) {
        return "Male";
    }
    return "";
}

std::string edgeVoiceLocale(const VoiceEntry& v) {
    // shortName: "<lang>-<REGION>-<Name>Neural" -> take the first two '-' tokens.
    const std::string& s = v.shortName;
    const size_t first = s.find('-');
    if (first == std::string::npos) {
        return "";
    }
    const size_t second = s.find('-', first + 1);
    if (second == std::string::npos) {
        return "";
    }
    return s.substr(0, second);
}

const std::vector<std::string>& edgeTtsLocales() {
    static const std::vector<std::string> locales = [] {
        std::vector<std::string> out;
        for (const auto& v : edgeTtsVoices()) {
            std::string loc = edgeVoiceLocale(v);
            if (!loc.empty() && std::find(out.begin(), out.end(), loc) == out.end()) {
                out.push_back(loc);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }();
    return locales;
}

} // namespace tts
