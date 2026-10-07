#include "core/Cli.hpp"

namespace jot::core {

namespace {
enum class Arg { Word, Append, List, Both, Other };

Arg kind(const std::string& a) {
    if (a == "-a" || a == "--append") return Arg::Append;
    if (a == "-l" || a == "--list") return Arg::List;
    if (a == "-al" || a == "-la") return Arg::Both;
    if (a.size() > 1 && a[0] == '-') return Arg::Other;
    return Arg::Word;
}
}  // namespace

std::vector<std::string> cli_join_append_list(const std::vector<std::string>& argv) {
    bool a = false, l = false, both = false;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        switch (kind(argv[i])) {
            case Arg::Append: a = true; break;
            case Arg::List:   l = true; break;
            case Arg::Both:   both = true; break;
            case Arg::Other:  return argv;   // --capture, --help...: not ours to rewrite
            case Arg::Word:   break;
        }
    }
    if (!both && !(a && l)) return argv;

    std::string name, line;
    std::vector<std::string> items;
    auto add_line = [&](const std::string& w) { line += (line.empty() ? "" : " ") + w; };

    if (both && !a && !l) {
        // -al: the name, then the line (one word or a quoted phrase), then items.
        std::vector<std::string> words;
        for (std::size_t i = 1; i < argv.size(); ++i)
            if (kind(argv[i]) == Arg::Word) words.push_back(argv[i]);
        if (!words.empty()) name = words[0];
        if (words.size() > 1) line = words[1];
        for (std::size_t i = 2; i < words.size(); ++i) items.push_back(words[i]);
    } else {
        // -a ... -l ... in either order: each flag owns the words after it.
        // (-al mixed in with them reads as the pair it is.)
        enum class To { Name, Line, Items } to = To::Name;
        for (std::size_t i = 1; i < argv.size(); ++i) {
            switch (kind(argv[i])) {
                case Arg::Append: to = To::Line; continue;
                case Arg::List:
                case Arg::Both:   to = To::Items; continue;
                default: break;
            }
            const std::string& w = argv[i];
            if (to == To::Name) {
                if (name.empty()) name = w; else add_line(w);   // stray words before a flag
            } else if (to == To::Line) {
                add_line(w);
            } else {
                items.push_back(w);
            }
        }
    }

    const std::string argv0 = argv.empty() ? std::string("jot") : argv[0];
    // s061f (Scott): an empty line -- `-al "" milk eggs` -- is "no line, these
    // are todos": a plain list. No items but a line: a plain append.
    const bool blank_line = line.find_first_not_of(" \t") == std::string::npos;
    if (blank_line && !items.empty() && !name.empty()) {
        std::vector<std::string> out{argv0, name, "--list"};
        out.insert(out.end(), items.begin(), items.end());
        return out;
    }
    if (!blank_line && items.empty() && !name.empty())
        return {argv0, name, "--append", line};
    std::vector<std::string> out{argv0, "--both"};
    if (!name.empty()) out.push_back(name);
    if (!line.empty()) out.push_back(line);
    out.insert(out.end(), items.begin(), items.end());
    return out;
}

}  // namespace jot::core
