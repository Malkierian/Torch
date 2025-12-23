#include "SpriteFactory.h"
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include <cstring>
#include "archive/SWrapper.h"

extern "C" {
#include "n64graphics/n64graphics.h"
}

namespace BK64 {

static const std::unordered_map <std::string, std::string> sTextureCTypes = {
    { "RGBA16", "u16" },
    { "RGBA32", "u16" },
    { "CI4", "u8" },
    { "CI8", "u8" },
    { "I4", "u8" },
    { "I8", "u8" },
    { "IA1", "u8" },
    { "IA4", "u8" },
    { "IA8", "u8" },
    { "IA16", "u16" },
    { "TLUT", "u16" },
};

static const std::unordered_map <std::string, TextureType> sTextureFormats = {
    { "RGBA16", TextureType::RGBA16bpp },
    { "RGBA32", TextureType::RGBA32bpp },
    { "CI4", TextureType::Palette4bpp },
    { "CI8", TextureType::Palette8bpp },
    { "I4", TextureType::Grayscale4bpp },
    { "I8", TextureType::Grayscale8bpp },
    { "IA1", TextureType::GrayscaleAlpha1bpp },
    { "IA4", TextureType::GrayscaleAlpha4bpp },
    { "IA8", TextureType::GrayscaleAlpha8bpp },
    { "IA16", TextureType::GrayscaleAlpha16bpp },
    { "TLUT", TextureType::TLUT },
};

#define ALIGN8(val) (((val) + 7) & ~7)

void ExtractChunk(LUS::BinaryReader& reader, std::vector<std::pair<int16_t, int16_t>>& positions, uint32_t& offset, std::string format, std::string symbol, uint32_t chunkNo) {
    reader.Seek(offset, LUS::SeekOffsetType::Start);

    int16_t x = reader.ReadInt16();
    int16_t y = reader.ReadInt16();
    int16_t width = reader.ReadInt16();
    int16_t height = reader.ReadInt16();

    positions.emplace_back(x, y);
    
    offset += 4 * sizeof(int16_t);
    offset = ALIGN8(offset);
    
    auto size = TextureUtils::CalculateTextureSize(sTextureFormats.at(format), width, height);

    YAML::Node texture;
    texture["type"] = "TEXTURE";
    texture["offset"] = offset;
    texture["format"] = format;
    if (format == "CI4" || format == "CI8") {
        texture["tlut_symbol"] = symbol + "TLUT";
    }
    texture["ctype"] = "u16";
    texture["width"] = width;
    texture["height"] = height;
    texture["symbol"] = symbol + std::to_string(chunkNo);
    
    Companion::Instance->AddAsset(texture);
    offset += size;
}

ExportResult SpriteHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult SpriteCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto sprite = std::static_pointer_cast<SpriteData>(raw);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    // TODO:

    write << "BKSpriteHeader " << symbol << "_Header = { " << sprite->mFrameCount << ", " << sprite->mFormatCode << " };\n\n";



    return offset;
}

ExportResult SpriteBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    auto writer = LUS::BinaryWriter();
    auto sprites = std::static_pointer_cast<SpriteData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKSprite, 0);
    
    auto wrapper = Companion::Instance->GetCurrentWrapper();

    writer.Write(sprites->mFormatCode);
    writer.Write((uint32_t)sprites->mPositions.size());
    for (auto position : sprites->mPositions) {
        writer.Write(position.first);
        writer.Write(position.second);
    }
    writer.Write((uint32_t)sprites->mChunkCounts.size());
    for (auto chunkCount : sprites->mChunkCounts) {
        writer.Write(chunkCount);
        // TODO: write texture hashes?
    }

    writer.Finish(write);

    return std::nullopt;
}

ExportResult SpriteModdingExporter::Export(std::ostream&write, std::shared_ptr<IParsedData> raw, std::string&entryName, YAML::Node&node, std::string* replacement) {
    // TODO: export X and Y Positions

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> SpriteFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto spriteOffset = GetSafeNode<uint32_t>(node, "offset"); // Should always be 0 in reality
    uint32_t offset;
    
    reader.SetEndianness(Torch::Endianness::Big);
    
    int16_t frameCount = reader.ReadInt16();
    int16_t formatCode = reader.ReadInt16();
    std::string format;

    switch (formatCode) {
        case 0x1:
            format = "CI4";
            break;
        case 0x4:
            format = "CI8";
            break;
        case 0x20:
            format = "I4";
            break;
        case 0x40:
            format = "I8";
            break;
        case 0x80:
            format = "IA4";
            break;
        case 0x100:
            format = "IA8";
            break;
        case 0x400:
            format = "RGBA16";
            break;
        case 0x800:
            format = "RGBA32";
            break;
        default:
            SPDLOG_WARN("UNRECOGNISED FORMAT 0x{:X}", formatCode);
            return std::nullopt;
    }

    std::vector<uint16_t> chunkCounts;
    std::vector<std::pair<int16_t, int16_t>> positions;

    if (frameCount > 0x100) {
        offset = spriteOffset + 8;
        std::string texSymbol = symbol + "_0_";
        ExtractChunk(reader, positions, offset, "RGBA16", texSymbol, 0);
        return std::make_shared<SpriteData>(frameCount, formatCode, chunkCounts, positions);
    }

    reader.Seek(0x10, LUS::SeekOffsetType::Start);
    std::vector<uint32_t> offsets;
    for (int16_t i = 0; i < frameCount; i++) {
        offsets.push_back(reader.ReadUInt32());
    }

    uint32_t frame = 0;
    for (const auto &frameOffset : offsets) {
        offset = spriteOffset + 0x10 + frameOffset + frameCount * sizeof(uint32_t);
        reader.Seek(offset - spriteOffset, LUS::SeekOffsetType::Start);
        int16_t x = reader.ReadInt16();
        int16_t y = reader.ReadInt16();
        int16_t width = reader.ReadInt16();
        int16_t height = reader.ReadInt16();
        uint16_t chunkCount = reader.ReadInt16();
        // TODO: Figure out the rest of the frame header
        reader.ReadInt16(); // pad?
        auto unkC = reader.ReadInt16();
        auto unkE = reader.ReadInt16();
        auto unk10 = reader.ReadInt16();
        auto unk12 = reader.ReadInt16();

        offset += 0x14;

        chunkCounts.push_back(chunkCount);
        
        if (format == "CI4" || format == "CI8") {
            offset = ALIGN8(offset);

            int16_t colors = (format == "CI4") ? 0x10 : 0x100;
            YAML::Node tlut;
            tlut["type"] = "TEXTURE";
            tlut["offset"] = offset;
            tlut["format"] = "TLUT";
            tlut["ctype"] = "u16";
            tlut["colors"] = colors;
            tlut["symbol"] = symbol + "_" + std::to_string(frame) + "_TLUT";
            Companion::Instance->AddAsset(tlut);

            offset += colors * sizeof(int16_t);
        }

        for (uint16_t i = 0; i < chunkCount; i++) {
            std::string texSymbol = symbol + "_" + std::to_string(frame) + "_";
            ExtractChunk(reader, positions, offset, format, texSymbol, i);
        }
        frame++;
    }

    return std::make_shared<SpriteData>(frameCount, formatCode, chunkCounts, positions);
}

} // namespace BK64
