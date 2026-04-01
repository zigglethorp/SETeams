#pragma once

#include "duosort_types.h"

#include <functional>
#include <string>
#include <vector>

// Persists the current photo catalog to SQLite when sqlite3 headers are available.
void WritePhotoCatalogDb(const std::vector<PhotoRecord>& photos,
                         const std::function<void(const std::string&)>& logger);
