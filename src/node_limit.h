/*
    Search node limit (--node-limit, --node-limits-file).

    The limit applies to the partisan sumgame search: when
    solver_stats::search_node_count reaches the current limit,
    sumgame::_over_time() returns true and the search aborts through the
    existing timeout path. The test case is then reported with status
    NODE_LIMIT.

    The limits file has one entry per line, either
        <case>,<limit>            applies to that 0-based case in every file
        <file>,<case>,<limit>     applies to that case in that file only
    where <file> is the path as printed in the CSV "File" column (relative to
    --test-dir). Blank lines and lines starting with '#' are ignored. Cases
    without an entry use --node-limit (0 = no limit).
*/
#pragma once

#include <cstdint>
#include <string>

namespace node_limit {

// Limit for the test case being run, 0 = none. Read by sumgame::_over_time()
extern uint64_t current_limit;

void load_limits_file(const std::string& file_name);

// Select the limit for a test case (autotests). Call before running it.
void set_current_case(const std::string& file, int case_number);

// Use global::node_limit() for whatever runs next
void reset_current_case();

inline bool active()
{
    return current_limit != 0;
}

// true IFF a limit is active and the node counter has reached it
bool reached();

} // namespace node_limit
