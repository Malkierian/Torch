#pragma once

#include "factories/BaseFactory.h"
#include "utils/TextureUtils.h"
#include <types/RawBuffer.h>
#include <unordered_map>
#include <string>
#include <vector>

namespace BK64 {

/**
 * SpriteData: 2D billboard sprite asset definition
 */
class SpriteData : public IParsedData {
public:
    int16_t mFrameCount; // Number of animation frames
    int16_t mFormatCode; // Texture format (RGBA16=0, RGBA32=1, CI4=2, CI8=3, etc.)
    std::vector<uint16_t> mChunkCounts; // Chunks per frame (length = mFrameCount)
    std::vector<std::pair<int16_t, int16_t>> mPositions; // (x, y) offset per chunk

    SpriteData(int16_t frameCount, int16_t formatCode, std::vector<uint16_t> chunkCounts, std::vector<std::pair<int16_t, int16_t>> positions) : mFrameCount(frameCount), mFormatCode(formatCode), mChunkCounts(std::move(chunkCounts)), mPositions(std::move(positions)) {}
};

class SpriteHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SpriteFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Header, SpriteHeaderExporter)
            REGISTER(Binary, SpriteBinaryExporter)
            REGISTER(Code, SpriteCodeExporter)
            REGISTER(Modding, SpriteModdingExporter)
        };
    }

    bool HasModdedDependencies() override { return true; }
    bool SupportModdedAssets() override { return true; }
};

} // namespace BK64
