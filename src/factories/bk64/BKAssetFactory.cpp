#include "BKAssetFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include <cstring>

namespace BK64 {

static const std::unordered_map<BKAssetType, std::string> sAssetSymbolPrefixes = {
    { BKAssetType::Animation, "ANIM" },
    { BKAssetType::Binary, "BIN" },
    { BKAssetType::DemoInput, "DEMO" },
    { BKAssetType::Dialog, "DIALOG" },
    { BKAssetType::GruntyQuestion, "GRUNTYQ" },
    { BKAssetType::Map, "MAP" },
    { BKAssetType::Midi, "MIDI" },
    { BKAssetType::Model, "MODEL" },
    { BKAssetType::QuizQuestion, "QUIZQ" },
    { BKAssetType::Sprite, "SPRITE" },
};

ExportResult BKAssetHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern BKAssetTableEntry " << symbol << "[];\n";
    return std::nullopt;
}

ExportResult BKAssetCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto assetTable = std::static_pointer_cast<BKAssetData>(raw);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "BKAssetTableEntry " << symbol << "[] = {\n";

    for (const auto& assetInfo : assetTable->mAssetTableInfo) {
        write << fourSpaceTab << "{ ";
        write << "/* index */ " << assetInfo.index << ", ";
        write << "/* offset */ " << assetInfo.offset << ", ";
        write << "/* compressed */ " << assetInfo.compressionFlag << ", ";
        write << "/* tFlag */ " << assetInfo.tFlag;
        write << " }, // mode: " << assetInfo.assetMode;
        
        if (assetInfo.tFlag == 4) {
            write << " (empty slot)";
        }
        
        write << "\n";
    }

    write << "};\n\n";

    // Calculate size: count + padding + entries
    size_t size = 4 + 4 + (assetTable->mAssetTableInfo.size() * 8);

    return offset + size;
}

ExportResult BKAssetBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto assetTable = std::static_pointer_cast<BKAssetData>(raw);

    WriteHeader(writer, Torch::ResourceType::Blob, 0);

    // Write asset ID → o2r path manifest.
    // BlobFactory reads a u32 dataSize first, then dataSize raw bytes.
    // Manifest payload: u32 count, then for each entry: u32 assetId, s32 pathLen, char path[pathLen]
    size_t payloadSize = 4; // u32 count
    for (const auto& [id, symbol] : assetTable->mSymbolMap) {
        payloadSize += 4 + 4 + symbol.size(); // u32 id + s32 pathLen + path bytes
    }
    writer.Write(static_cast<uint32_t>(payloadSize));

    // Manifest payload
    writer.Write(static_cast<uint32_t>(assetTable->mSymbolMap.size()));
    for (const auto& [id, symbol] : assetTable->mSymbolMap) {
        writer.Write(id);
        writer.Write(symbol);  // writes s32 length prefix + string bytes
    }

    writer.Finish(write);
    return OffsetEntry{ 0 };
}

std::optional<std::shared_ptr<IParsedData>> BKAssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    bool symbolMapExists = false;
    if (node["symbol_map"]) {
        symbolMapExists = true;
    }
    
    reader.SetEndianness(Torch::Endianness::Big);
    
    uint32_t assetCount = reader.ReadUInt32();
    reader.ReadUInt32();

    uint32_t dataStartRomOffset = offset + 8 + assetCount * 8;
    int16_t prevTFlag = 3;
    int32_t assetMode = 0;

    std::vector<BKAssetInfo> assetTableInfo;
    std::unordered_map<uint32_t, std::string> symbolMap;

    for (uint32_t i = 0; i < assetCount; i++) {
        BKAssetInfo assetInfo;
        assetInfo.index = i;
        assetInfo.offset = reader.ReadUInt32();
        assetInfo.compressionFlag = reader.ReadInt16();
        assetInfo.tFlag = reader.ReadInt16();

        if (assetInfo.tFlag == 4) {
            // Empty Asset Slot
            // May Need to add in order to get offset size
            assetTableInfo.emplace_back(assetInfo);
            continue;
        }

        if (assetInfo.tFlag != 2 && (prevTFlag & 2) != (assetInfo.tFlag & 2)) {
            assetMode++;
            prevTFlag = assetInfo.tFlag;
        }

        assetInfo.assetMode = assetMode;

        assetTableInfo.emplace_back(assetInfo);
    }

    int count = 0;

    for (uint32_t i = 0; i < assetCount - 1; i++) {
        auto assetInfo = assetTableInfo.at(i);
        
        // Always start with the next asset's offset as baseline
        uint32_t assetSize = assetTableInfo.at(i + 1).offset - assetInfo.offset;
        
        // If the next asset has the same offset (empty slot), find the next one with a different offset
        // This gives us the true size boundary between asset groups
        if (assetSize == 0) {
            for (uint32_t j = i + 2; j < assetCount; j++) {
                if (assetTableInfo.at(j).offset != assetInfo.offset) {
                    assetSize = assetTableInfo.at(j).offset - assetInfo.offset;
                    break;
                }
            }
        }
        
        auto assetOffset = dataStartRomOffset + assetInfo.offset;
        BKAssetType assetType;

        if (assetInfo.tFlag == 4) {
            continue;
        }

        switch (assetInfo.assetMode) {
            case 0:
                assetType = BKAssetType::Animation;
                break;
            case 1:
            case 3: {
                uint8_t* dataBuf;
                if (assetInfo.compressionFlag != 0) {
                    DataChunk* uncompressedData = Decompressor::Decode(buffer, assetOffset, CompressionType::BKZIP, assetSize);
                    dataBuf = uncompressedData->data;
                } else {
                    dataBuf = buffer.data() + assetOffset;
                }
                if (dataBuf[0] == 0 && dataBuf[1] == 0 && dataBuf[2] == 0 && dataBuf[3] == 11) {
                    assetType = BKAssetType::Model;
                } else {
                    assetType = BKAssetType::Sprite;
                }
                break;
            }
            case 2:
                assetType = BKAssetType::Map;
                break;
            case 4: {
                uint8_t* dataBuf;
                if (assetInfo.compressionFlag != 0) {
                    DataChunk* uncompressedData = Decompressor::Decode(buffer, assetOffset, CompressionType::BKZIP, assetSize);
                    dataBuf = uncompressedData->data;
                } else {
                    dataBuf = buffer.data() + assetOffset;
                }
                if (dataBuf[0] == 1 && dataBuf[1] == 1 && dataBuf[2] == 2 && dataBuf[3] == 5 && dataBuf[4] == 0) {
                    assetType = BKAssetType::QuizQuestion;
                } else if (dataBuf[0] == 1 && dataBuf[1] == 3 && dataBuf[2] == 0 && dataBuf[3] == 5 && dataBuf[4] == 0) {
                    assetType = BKAssetType::GruntyQuestion;
                } else if (dataBuf[0] == 1 && dataBuf[1] == 3 && dataBuf[2] == 0) {
                    assetType = BKAssetType::Dialog;
                } else {
                    assetType = BKAssetType::DemoInput;
                }
                break;
            }
            case 5:
                assetType = BKAssetType::Model;
                break;
            case 6:
                assetType = BKAssetType::Midi;
                break;
            default:
                assetType = BKAssetType::Binary;
                break;
        }

        std::string assetSymbol;
        std::string assetIndexStr = std::to_string(assetInfo.index);

        if (symbolMapExists && node["symbol_map"][assetIndexStr]) {
            assetSymbol = node["symbol_map"][assetIndexStr].as<std::string>();
        } else {
            std::stringstream assetStream;

            assetStream << "D_" << sAssetSymbolPrefixes.at(assetType) << "_" << std::to_string(assetInfo.index);

            assetSymbol = assetStream.str();
        }

        symbolMap[assetInfo.index] = assetSymbol;

        YAML::Node bkAssetNode;
        bkAssetNode["offset"] = assetOffset;
        bkAssetNode["symbol"] = assetSymbol;
        CompressionType compressionType = (assetInfo.compressionFlag != 0) ? CompressionType::BKZIP : CompressionType::None;

        // Uncomment AddAssets when Factory is implemented
        switch (assetType) {
            case BKAssetType::Animation:
                bkAssetNode["type"] = "BK64:ANIM";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Binary:
                bkAssetNode["type"] = "BLOB";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::DemoInput:
                bkAssetNode["type"] = "BK64:DEMO";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Dialog:
                bkAssetNode["type"] = "BK64:DIALOG";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::GruntyQuestion:
                bkAssetNode["type"] = "BK64:GRUNTYQ";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Map:
                bkAssetNode["type"] = "BK64:MAP";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Midi:
                bkAssetNode["type"] = "BLOB";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Model:
                bkAssetNode["type"] = "BK64:MODEL";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::QuizQuestion:
                bkAssetNode["type"] = "BK64:QUIZQ";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            case BKAssetType::Sprite:
                bkAssetNode["type"] = "BK64:SPRITE";
                Companion::Instance->AddSubFileAsset(bkAssetNode, assetSymbol, compressionType, assetSize);
                break;
            default:
                // Should be unreachable
                throw std::runtime_error("Invalid BKAsset Type Found");
        }
    }

    return std::make_shared<BKAssetData>(assetTableInfo, symbolMap);
}

} // namespace BK64
