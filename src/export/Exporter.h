#pragma once
#include "pdb/SymbolNode.h"

#include <expected>
#include <string>
#include <vector>

class PdbIndex;

// Controls which nested data sections are included in the export.
struct ExportOptions {
    bool includeMembers      = true;   // UDT data members
    bool includeBaseClasses  = true;   // UDT base classes
    bool includeFriends      = true;   // UDT friend declarations
    bool includeEnumValues   = true;   // Enum enumerator name/value pairs
    bool includeSourceFiles  = true;   // Compiland source file list
    bool includeTemplateArgs = true;   // Function/UDT template arguments
};

class Exporter {
public:
    // Export the given symbol IDs to a JSON file.
    static std::expected<void, std::string> toJson(const PdbIndex& index,
                                                    const std::vector<SymbolId>& ids,
                                                    const std::string& outputPath,
                                                    const ExportOptions& options = {});

    // Export to a flat CSV file.
    static std::expected<void, std::string> toCsv(const PdbIndex& index,
                                                   const std::vector<SymbolId>& ids,
                                                   const std::string& outputPath,
                                                   const ExportOptions& options = {});
};
