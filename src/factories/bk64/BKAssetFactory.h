#pragma once

#include "factories/BaseFactory.h"
#include <types/RawBuffer.h>
#include <unordered_map>
#include <string>
#include <vector>

namespace BK64 {

/**
 * BKAssetInfo: Asset table entry defining location and type
 * 
 * Runtime Purpose:
 * - Central asset registry mapping Asset IDs to ROM/file offsets
 * - Loaded at boot via func_80324D44_loadAssetTable
 * - All asset loads (models, sprites, animations, levels) go through this table
 * - Asset ID → lookup in table → get offset → decompress → return data
 * 
 * Fields:
 * - offset: ROM address or file offset (BKZIP compressed if compressionFlag != 0)
 * - compressionFlag: 0 = uncompressed, 1 = BKZIP compressed, 2 = MIO0, 3 = YAY0
 * - tFlag: Type discriminator (0=model, 1=sprite, 2=animation, etc.)
 * - assetMode: Loading mode (0=immediate, 1=deferred, 2=cached)
 * - index: Original asset index in source data (debugging aid)
 * 
 * Usage Flow:
 *   getModel3d(0x2d5) → assetTable[0x2d5] → offset=0x1A2400, compression=1
 *   → Decompressor::BKZIP(ROM + 0x1A2400) → ModelFactory::parse()
 */
typedef struct BKAssetInfo {
    uint32_t offset;          // ROM/file offset to asset data
    int16_t compressionFlag;  // Compression type (0=none, 1=BKZIP, 2=MIO0, 3=YAY0)
    int16_t tFlag;            // Type flag (asset category)
    int32_t assetMode;        // Loading mode (immediate/deferred/cached)
    int32_t index;            // Original asset index
} BKAssetInfo;

/**
 * BKAssetType: Asset category enumeration
 * 
 * Runtime Purpose:
 * - Determines which factory/loader to use for asset data
 * - Corresponds to different overlay functions for specialized loading
 * 
 * Type Ranges (Asset ID → Type):
 *   0x000 - 0x0FF: Animations (skeletal animation data)
 *   0x100 - 0x1FF: Binary (raw data blocks, overlay code)
 *   0x200 - 0x2CF: DemoInput (attract mode recordings)
 *   0x2D0 - 0x2D0: Dialog (Bottles' tutorials, character speech)
 *   0x2D1 - 0x571: Model (3D geometry, collision, textures)
 *   0x572 - 0x6FF: Sprite (2D billboards, UI elements)
 *   0x700 - 0x7FF: Map (cube spatial data)
 *   0x800 - 0x8FF: Midi (background music sequences)
 *   0x900 - 0x9FF: GruntyQuestion (Grunty's Furnace Fun questions)
 *   0xA00 - 0xAFF: QuizQuestion (quiz show questions)
 */
enum class BKAssetType {
    Animation,
    Binary,
    DemoInput,
    Dialog,
    GruntyQuestion,
    Map,
    Midi,
    Model,
    QuizQuestion,
    Sprite,
};

class BKAssetData : public IParsedData {
public:
    std::vector<BKAssetInfo> mAssetTableInfo;

    BKAssetData(std::vector<BKAssetInfo> assetTableInfo) : mAssetTableInfo(std::move(assetTableInfo)) {}
};

class BKAssetHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BKAssetBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BKAssetCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class BKAssetFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, BKAssetHeaderExporter)
            REGISTER(Binary, BKAssetBinaryExporter)
            REGISTER(Code, BKAssetCodeExporter)
        };
    }

    bool HasModdedDependencies() override { return true; }
};

} // namespace BK64
