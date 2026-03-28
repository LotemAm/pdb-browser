#include "PdbIndex.h"

void PdbIndex::reserve(size_t estimatedSymbols)
{
    m_symbols.reserve(estimatedSymbols);
    m_pdbIdIndex.reserve(estimatedSymbols);
    m_nameIndex.reserve(estimatedSymbols);
}

SymbolId PdbIndex::addSymbol(SymbolNode node)
{
    // Deduplicate by PDB-internal ID — DIA can return the same symbol
    // from multiple enumeration paths (global scope, compiland children, etc.)
    if (node.pdbId != 0) {
		auto symId = findByPdbId(node.pdbId);
        if (symId != INVALID_SYMBOL_ID)
            return symId;
    }

    SymbolId id = static_cast<SymbolId>(m_symbols.size());
    node.id = id;

    // Maintain secondary indexes
    m_pdbIdIndex.emplace(node.pdbId, id);

    if (!node.name.empty())
        m_nameIndex[node.name].push_back(id);

    if (node.rva != 0)
        m_rvaIndex.emplace(node.rva, id);

    switch (node.kind) {
    case SymbolKind::Compiland:    m_compilands.push_back(id); break;
    case SymbolKind::UDT:          m_udts.push_back(id);       break;
    case SymbolKind::Enum:         m_enums.push_back(id);      break;
    case SymbolKind::Typedef:      m_typedefs.push_back(id);   break;
    case SymbolKind::Function:     m_functions.push_back(id);  break;
    case SymbolKind::Data:
    case SymbolKind::PublicSymbol: m_globals.push_back(id);    break;
    default: break;
    }

    m_symbols.push_back(std::move(node));
    return id;
}

// getSymbol is now defined in the header via deducing this.

std::vector<SymbolId> PdbIndex::findByName(const std::string& name) const
{
    auto it = m_nameIndex.find(name);
    if (it == m_nameIndex.end()) return {};
    return it->second;
}

SymbolId PdbIndex::findByRva(uint32_t rva) const
{
    auto it = m_rvaIndex.find(rva);
    return it != m_rvaIndex.end() ? it->second : INVALID_SYMBOL_ID;
}

SymbolId PdbIndex::findByPdbId(PdbId pdbId) const
{
    auto it = m_pdbIdIndex.find(pdbId);
    return it != m_pdbIdIndex.end() ? it->second : INVALID_SYMBOL_ID;
}
