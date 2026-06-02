#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

class EnG2P {
public:
    EnG2P(const std::string& dict_path) {
        load_dict(dict_path);
        init_arpabet_map();
    }

    std::string strip_stress(std::string str) {
        size_t pos = 0;
        while ((pos = str.find("ˈ")) != std::string::npos) {
            str.erase(pos, 2); // "ˈ" is 2 bytes in UTF-8
        }
        pos = 0;
        while ((pos = str.find("ˌ")) != std::string::npos) {
            str.erase(pos, 2); // "ˌ" is 2 bytes in UTF-8
        }
        return str;
    }

    std::string convert(const std::string& word) {
        std::string upper_word = word;
        // Simple strip of punctuation if needed? 
        // But let's just assume the input is somewhat clean or strict match first.
        
        // Trim common punctuation from ends just in case
        size_t start = 0;
        while (start < upper_word.size() && !isalnum((unsigned char)upper_word[start])) start++;
        size_t end = upper_word.size();
        while (end > start && !isalnum((unsigned char)upper_word[end-1])) end--;
        
        std::string clean_word = upper_word.substr(start, end - start);
        std::string prefix = upper_word.substr(0, start);
        std::string suffix = upper_word.substr(end);
        
        std::transform(clean_word.begin(), clean_word.end(), clean_word.begin(), ::toupper);
        
        // std::cout << "Debug EnG2P: Query [" << clean_word << "]" << std::endl;
        
        if (dict_.count(clean_word)) {
            std::string ipa = arpabet_to_ipa(dict_.at(clean_word));
            
            // Apply weak form reductions and destressing for English function words
            if (clean_word == "TO") {
                ipa = "tə";
            } else if (clean_word == "OF") {
                ipa = "əv";
            } else if (clean_word == "FOR") {
                ipa = "fɚ";
            } else if (clean_word == "AND") {
                ipa = "ənd";
            } else if (clean_word == "AT") {
                ipa = "ət";
            } else if (clean_word == "FROM") {
                ipa = "frəm";
            } else if (clean_word == "BUT") {
                ipa = "bət";
            } else {
                static const std::unordered_set<std::string> function_words = {
                    "IT", "IS", "YOU", "ME", "HIM", "HER", "THEM", "US", "ITS", "YOUR", "MY", "THEIR", "OUR", "HIS",
                    "THIS", "THAT", "THESE", "THOSE", "WHO", "WHOM", "WHOSE", "WHICH", "WHAT", "SOME", "ANY", "NO",
                    "EACH", "EVERY", "ALL", "BOTH", "SUCH", "OTHER", "ANOTHER", "BE", "BEEN", "AM", "ARE", "WAS", "WERE",
                    "HAS", "HAVE", "HAD", "DO", "DOES", "DID", "CAN", "COULD", "SHALL", "SHOULD", "WILL", "WOULD", "MAY",
                    "MIGHT", "MUST", "AN", "AS", "BY", "IF", "OR", "SO", "THAN", "UP", "WITH", "ABOUT", "THERE", "THEY",
                    "HE", "SHE", "I", "WE"
                };
                if (function_words.count(clean_word)) {
                    ipa = strip_stress(ipa);
                }
            }
            
            return prefix + ipa + suffix;
        }
        
        // Fallback: return original
        return word; 
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> dict_;
    std::unordered_map<std::string, std::string> arpabet_map_;

    void load_dict(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            // Silent fail or log?
            std::cerr << "[EnG2P] Warning: Failed to open CMU dict: " << path << std::endl;
            return;
        }
        std::string line;
        int count = 0;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            // CMU dict lines start with word, possibly with symbols like !EXCLAMATION-POINT
            // Standard format: WORD  PH ON E M ES
            if (!isalpha(line[0]) && line[0] != '\'') continue; // Basic filtering

            std::stringstream ss(line);
            std::string word, ph;
            ss >> word;
            
            // Handle variants like WORD(1)
            size_t paren = word.find('(');
            if (paren != std::string::npos) {
                word = word.substr(0, paren);
            }

            // Normalize to UPPERCASE
            std::transform(word.begin(), word.end(), word.begin(), ::toupper);

            std::vector<std::string> phonemes;
            while (ss >> ph) {
                phonemes.push_back(ph);
            }
            
            // Only keep first variant if multiple exist (CMU dict is sorted, usually main first)
            if (!dict_.count(word)) {
                dict_[word] = phonemes;
                count++;
                // if (count < 5) std::cout << "Debug CMU: Loaded [" << word << "]" << std::endl;
            }
        }
        std::cout << "[EnG2P] Loaded " << dict_.size() << " words from CMU dict." << std::endl;
        
        // Add apostrophe-less contraction fallbacks to prevent CppJieba segmentation splits
        std::vector<std::pair<std::string, std::string>> contraction_mappings = {
            {"ISNT", "ISN'T"},
            {"DONT", "DON'T"},
            {"CANT", "CAN'T"},
            {"WONT", "WON'T"},
            {"DOESNT", "DOESN'T"},
            {"DIDNT", "DIDN'T"},
            {"ARENT", "AREN'T"},
            {"WASNT", "WASN'T"},
            {"WERENT", "WEREN'T"},
            {"HAVENT", "HAVEN'T"},
            {"HASNT", "HASN'T"},
            {"HADNT", "HADN'T"},
            {"SHOULDNT", "SHOULDN'T"},
            {"WOULDNT", "WOULDN'T"},
            {"COULDNT", "COULDN'T"},
            {"MUSTNT", "MUSTN'T"},
            {"IM", "I'M"},
            {"ITS", "IT'S"},
            {"THATS", "THAT'S"},
            {"THERES", "THERE'S"},
            {"WHATS", "WHAT'S"},
            {"LETS", "LET'S"},
            {"HERES", "HERE'S"},
            {"HOWS", "HOW'S"},
            {"WHOS", "WHO'S"},
            {"YOURE", "YOU'RE"},
            {"THEYRE", "THEY'RE"},
            {"IVE", "I'VE"},
            {"YOUVE", "YOU'VE"},
            {"WEVE", "WE'VE"},
            {"THEYVE", "THEY'VE"},
            {"ITLL", "IT'LL"}
        };
        for (const auto& pair : contraction_mappings) {
            if (dict_.count(pair.second)) {
                dict_[pair.first] = dict_.at(pair.second);
            }
        }
    }

    void init_arpabet_map() {
        // Mapping ARPABET to IPA
        // Note: This is a simplified mapping.
        // Stress: 1 (primary) -> ˈ, 2 (secondary) -> ˌ, 0 (unstressed) -> nothing/schwa
        arpabet_map_ = {
            {"AA0", "ɑ"}, {"AA1", "ˈɑ"}, {"AA2", "ˌɑ"},
            {"AE0", "æ"}, {"AE1", "ˈæ"}, {"AE2", "ˌæ"},
            {"AH0", "ə"}, {"AH1", "ˈʌ"}, {"AH2", "ˌʌ"},
            {"AO0", "ɔ"}, {"AO1", "ˈɔ"}, {"AO2", "ˌɔ"},
            {"AW0", "aʊ"}, {"AW1", "ˈaʊ"}, {"AW2", "ˌaʊ"},
            {"AY0", "aɪ"}, {"AY1", "ˈaɪ"}, {"AY2", "ˌaɪ"},
            {"B", "b"}, {"CH", "tʃ"}, {"D", "d"}, {"DH", "ð"},
            {"EH0", "ɛ"}, {"EH1", "ˈɛ"}, {"EH2", "ˌɛ"},
            {"ER0", "ɚ"}, {"ER1", "ˈɝ"}, {"ER2", "ˌɝ"},
            {"EY0", "eɪ"}, {"EY1", "ˈeɪ"}, {"EY2", "ˌeɪ"},
            {"F", "f"}, {"G", "ɡ"}, {"HH", "h"},
            {"IH0", "ɪ"}, {"IH1", "ˈɪ"}, {"IH2", "ˌɪ"},
            {"IY0", "i"}, {"IY1", "ˈi"}, {"IY2", "ˌi"},
            {"JH", "dʒ"}, {"K", "k"}, {"L", "l"},
            {"M", "m"}, {"N", "n"}, {"NG", "ŋ"},
            {"OW0", "oʊ"}, {"OW1", "ˈoʊ"}, {"OW2", "ˌoʊ"},
            {"OY0", "ɔɪ"}, {"OY1", "ˈɔɪ"}, {"OY2", "ˌɔɪ"},
            {"P", "p"}, {"R", "r"}, {"S", "s"}, {"SH", "ʃ"},
            {"T", "t"}, {"TH", "θ"},
            {"UH0", "ʊ"}, {"UH1", "ˈʊ"}, {"UH2", "ˌʊ"},
            {"UW0", "u"}, {"UW1", "ˈu"}, {"UW2", "ˌu"},
            {"V", "v"}, {"W", "w"}, {"Y", "j"}, {"Z", "z"}, {"ZH", "ʒ"}
        };
    }

    std::string arpabet_to_ipa(const std::vector<std::string>& phonemes) {
        std::string res;
        for (const auto& p : phonemes) {
            if (arpabet_map_.count(p)) {
                res += arpabet_map_.at(p);
            } else {
                // Fallback: try removing digit
                std::string base = p;
                if (!base.empty() && isdigit(base.back())) base.pop_back();
                if (arpabet_map_.count(base)) {
                     res += arpabet_map_.at(base);
                }
            }
        }
        return res;
    }
};
