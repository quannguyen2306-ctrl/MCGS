#include "node_limit.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "global_options.h"
#include "solver_stats.h"
#include "string_to_int.h"
#include "utilities.h"

using namespace std;

namespace node_limit {

uint64_t current_limit = 0;

namespace {

map<int, uint64_t> case_limits;                      // <case>,<limit>
map<pair<string, int>, uint64_t> file_case_limits;   // <file>,<case>,<limit>

string trim(const string& s)
{
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return "";

    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

vector<string> split_commas(const string& line)
{
    vector<string> fields;
    size_t start = 0;

    while (true)
    {
        const size_t comma = line.find(',', start);

        if (comma == string::npos)
        {
            fields.push_back(trim(line.substr(start)));
            break;
        }

        fields.push_back(trim(line.substr(start, comma - start)));
        start = comma + 1;
    }

    return fields;
}

} // namespace

void load_limits_file(const string& file_name)
{
    ifstream infile(file_name);

    if (!infile.is_open())
        throw runtime_error("Error: --node-limits-file: couldn't open \"" +
                            file_name + "\"");

    string line;
    int line_no = 0;

    while (getline(infile, line))
    {
        line_no++;
        const string trimmed = trim(line);

        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        const vector<string> fields = split_commas(trimmed);
        const string where =
            " (" + file_name + ":" + to_string(line_no) + ")";

        try
        {
            if (fields.size() == 2)
            {
                const int case_number = str_to_i(fields[0]);
                const uint64_t limit = str_to_ull(fields[1]);
                case_limits[case_number] = limit;
                continue;
            }

            if (fields.size() == 3)
            {
                const int case_number = str_to_i(fields[1]);
                const uint64_t limit = str_to_ull(fields[2]);
                file_case_limits[{fields[0], case_number}] = limit;
                continue;
            }
        }
        catch (const exception& exc)
        {
            throw runtime_error("Error: --node-limits-file: bad number" +
                                where);
        }

        throw runtime_error("Error: --node-limits-file: expected "
                            "\"case,limit\" or \"file,case,limit\"" +
                            where);
    }
}

void set_current_case(const string& file, int case_number)
{
    auto file_it = file_case_limits.find({file, case_number});

    if (file_it != file_case_limits.end())
    {
        current_limit = file_it->second;
        return;
    }

    auto case_it = case_limits.find(case_number);

    if (case_it != case_limits.end())
    {
        current_limit = case_it->second;
        return;
    }

    current_limit = global::node_limit();
}

void reset_current_case()
{
    current_limit = global::node_limit();
}

bool reached()
{
    return active() &&
           stats::get_global_stats().search_node_count >= current_limit;
}

} // namespace node_limit
