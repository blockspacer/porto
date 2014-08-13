#include <sstream>

#include "stringutil.hpp"

using namespace std;

string CommaSeparatedList(const vector<string> &list) {
    string ret;
    for (auto c = list.begin(); c != list.end(); ) {
        ret += *c;
        if (++c != list.end())
            ret += ",";
    }
    return ret;
}

string CommaSeparatedList(const set<string> &list) {
    string ret;
    for (auto c = list.begin(); c != list.end(); ) {
        ret += *c;
        if (++c != list.end())
            ret += ",";
    }
    return ret;
}

TError StringsToIntegers(const std::vector<std::string> &strings,
                         std::vector<int> &integers) {
    for (auto l : strings) {
        try {
            integers.push_back(stoi(l));
        } catch (...) {
            return TError(TError::Unknown, "Bad integer value");
        }
    }

    return NoError;
}

TError StringToUint64(const std::string &string, uint64_t &value) {
    try {
        value = stoull(string);
    } catch (...) {
        return TError(TError::Unknown, "Bad integer value");
    }

    return NoError;
}

TError SplitString(const std::string &s, const char sep, std::vector<std::string> &tokens) {
    try {
        istringstream ss(s);
        string tok;

        while(std::getline(ss, tok, sep))
            tokens.push_back(tok);
    } catch (...) {
        return TError(TError::Unknown, "Can't split string");
    }

    return NoError;
}
