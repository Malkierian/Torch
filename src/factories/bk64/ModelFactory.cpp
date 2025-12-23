#include "ModelFactory.h"

#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "types/RawBuffer.h"

#define BK64_MODEL_HEADER 0xB
#define TEXTURE_HEADER_SIZE 0x8
#define TEXTURE_METADATA_SIZE 0x10
#define GFX_HEADER_SIZE 0x8
#define GFX_CMD_SIZE 0x8
#define VTX_HEADER_SIZE 0x18
#define ANIM_TEXTURE_LIST_COUNT 4

namespace BK64 {

static const std::unordered_map<std::string, uint8_t> gF3DTable = {
    { "G_VTX", 0x04 },
    { "G_DL", 0x06 },
    { "G_MTX", 0x1 },
    { "G_ENDDL", 0xB8 },
    { "G_SETTIMG", 0xFD },
    { "G_MOVEMEM", 0x03 },
    { "G_MV_L0", 0x86 },
    { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA },
    { "G_TRI2", 0xB1 },
    { "G_QUAD", -1 }
};

static const std::unordered_map<std::string, uint8_t> gF3DExTable = {
    { "G_VTX", 0x04 },
    { "G_DL", 0x06 },
    { "G_MTX", 0x1 },
    { "G_ENDDL", 0xB8 },
    { "G_SETTIMG", 0xFD },
    { "G_MOVEMEM", 0x03 },
    { "G_MV_L0", 0x86 },
    { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA },
    { "G_TRI2", 0xB1 },
    { "G_QUAD", 0xB5 }
};

static const std::unordered_map<std::string, uint8_t> gF3DEx2Table = {
    { "G_VTX", 0x01 },
    { "G_DL", 0xDE },
    { "G_MTX", 0xDA },
    { "G_ENDDL", 0xDF },
    { "G_SETTIMG", 0xFD },
    { "G_MOVEMEM", 0xDC },
    { "G_MV_L0", 0x86 },
    { "G_MV_L1", 0x88 },
    { "G_MV_LIGHT", 0xA },
    { "G_TRI2", 0x06 },
    { "G_QUAD", 0x07 }
};

static const std::unordered_map<GBIVersion, std::unordered_map<std::string, uint8_t>> gGBITable = {
    { GBIVersion::f3d, gF3DTable },
    { GBIVersion::f3dex, gF3DExTable },
    { GBIVersion::f3dex2, gF3DEx2Table },
};

#define GBI(cmd) gGBITable.at(Companion::Instance->GetGBIVersion()).at(#cmd)

ExportResult ModelHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    return std::nullopt;
}

ExportResult ModelCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto model = std::static_pointer_cast<ModelData>(raw);

    return offset;
}

ExportResult BK64::ModelBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto model = std::static_pointer_cast<ModelData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKModel, 0);
    
    auto wrapper = Companion::Instance->GetCurrentWrapper();

    writer.Write(1);

    writer.Finish(write);

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> ModelFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(Torch::Endianness::Big);
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto modelOffset = GetSafeNode<uint32_t>(node, "offset"); // Should always be 0 in reality
    const auto modelOffsetEnd = modelOffset + segment.size;
    const auto fileOffset = Companion::Instance->GetCurrentVRAM().value().offset;

    if (reader.ReadInt32() != BK64_MODEL_HEADER) {
        SPDLOG_ERROR("Invalid Header For BK64 Model {}", symbol);
        return std::nullopt;
    }

    /* 0x04 */ auto geoLayoutOffset = reader.ReadUInt32();
    /* 0x08 */ auto textureSetupOffset = reader.ReadUInt16();
    /* 0x0A */ auto geoType = reader.ReadUInt16();
    /* 0x0C */ auto displayListSetupOffset = reader.ReadUInt32();
    /* 0x10 */ auto vertexSetupOffset = reader.ReadUInt32();
    /* 0x14 */ auto unkHitboxInfoOffset = reader.ReadUInt32();
    /* 0x18 */ auto animationSetupOffset = reader.ReadUInt32();
    /* 0x1C */ auto collisionSetupOffset = reader.ReadUInt32();
    /* 0x20 */ auto modelUnk20Offset = reader.ReadUInt32();
    /* 0x24 */ auto effectsSetupOffset = reader.ReadUInt32();
    /* 0x28 */ auto modelUnk28Offset = reader.ReadUInt32();
    /* 0x2C */ auto animatedTextureOffset = reader.ReadUInt32();
    /* 0x30 */ auto triCount = reader.ReadUInt16();
    /* 0x32 */ auto vertCount = reader.ReadUInt16();

    uint16_t textureCount;
    
    if (geoLayoutOffset != 0) {
        SPDLOG_INFO("HAS GL {}", symbol);
        YAML::Node geoLayout;
        geoLayout["type"] = "BK64:GEO_LAYOUT";
        geoLayout["offset"] = modelOffset + geoLayoutOffset;
        geoLayout["symbol"] = symbol + "_GEO";
        Companion::Instance->AddAsset(geoLayout);
    }

    if (textureSetupOffset != 0) {

        reader.Seek(modelOffset + textureSetupOffset, LUS::SeekOffsetType::Start);

        auto textureDataSize = reader.ReadUInt32();
        textureCount = reader.ReadUInt16();
        reader.ReadUInt16(); // pad

        Companion::Instance->SetCompressedSegment(2, fileOffset, modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE + textureCount * TEXTURE_METADATA_SIZE);
        
        for (uint16_t i = 0; i < textureCount; i++) {
            auto textureDataOffset = reader.ReadUInt32();
            auto textureType = reader.ReadUInt16();
            reader.ReadUInt16(); // pad
            uint32_t width = reader.ReadUByte();
            uint32_t height = reader.ReadUByte();
            reader.ReadUInt16(); // pad
            reader.ReadUInt32(); // pad

            std::string format;
            std::string ctype;
            uint32_t tlutSize = 0;
            
            YAML::Node texture;
            switch (textureType) {
                case 0x1: {
                    uint32_t tlutOffset = modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE + textureCount * TEXTURE_METADATA_SIZE + textureDataOffset;
                    YAML::Node tlut;
                    tlut["type"] = "TEXTURE";
                    tlut["format"] = "TLUT";
                    tlut["ctype"] = "u16";
                    tlut["colors"] = 0x10;
                    tlut["offset"] = tlutOffset;
                    tlut["symbol"] = symbol + "_TLUT_" + std::to_string(i);
                    Companion::Instance->AddAsset(tlut);
                    texture["format"] = "CI4";
                    texture["ctype"] = "u8";
                    texture["tlut"] = tlutOffset;
                    tlutSize = 0x10;
                    break;
                }
                case 0x2: {
                    uint32_t tlutOffset = modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE + textureCount * TEXTURE_METADATA_SIZE + textureDataOffset;
                    YAML::Node tlut;
                    tlut["type"] = "TEXTURE";
                    tlut["format"] = "TLUT";
                    tlut["ctype"] = "u16";
                    tlut["colors"] = 0x100;
                    tlut["offset"] = tlutOffset;
                    tlut["symbol"] = symbol + "_TLUT_" + std::to_string(i);
                    Companion::Instance->AddAsset(tlut);
                    texture["format"] = "CI8";
                    texture["ctype"] = "u8";
                    texture["tlut"] = tlutOffset;
                    tlutSize = 0x100;
                    break;
                }
                case 0x4:
                    texture["format"] = "RGBA16";
                    texture["ctype"] = "u16";
                    break;
                case 0x8:
                    texture["format"] = "RGBA32";
                    texture["ctype"] = "u32";
                    break;
                case 0x10:
                    texture["format"] = "IA8";
                    texture["ctype"] = "u8";
                    break;
                default:
                    throw std::runtime_error("BK64::ModelFactory: Invalid Texture Format Found " + std::to_string(textureType));
            }

            uint32_t textureOffset = modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE + textureCount * TEXTURE_METADATA_SIZE + textureDataOffset + tlutSize * sizeof(int16_t);
            texture["type"] = "TEXTURE";
            texture["width"] = width;
            texture["height"] = height;
            texture["offset"] = textureOffset;
            texture["symbol"] = symbol + "_TEX_" + std::to_string(i);
            Companion::Instance->AddAsset(texture);
        }
    }

    // Parse First To Avoid Auto Extraction By DLs
    if (vertexSetupOffset != 0) {
        reader.Seek(modelOffset + vertexSetupOffset, LUS::SeekOffsetType::Start);
        Companion::Instance->SetCompressedSegment(1, fileOffset, modelOffset + vertexSetupOffset + VTX_HEADER_SIZE);

        auto minCoordsX = reader.ReadInt16();
        auto minCoordsY = reader.ReadInt16();
        auto minCoordsZ = reader.ReadInt16();
        auto maxCoordsX = reader.ReadInt16();
        auto maxCoordsY = reader.ReadInt16();
        auto maxCoordsZ = reader.ReadInt16();
        auto centerCoordsX = reader.ReadInt16();
        auto centerCoordsY = reader.ReadInt16();
        auto centerCoordsZ = reader.ReadInt16();

        auto largestDistToCenter = reader.ReadUInt16();
        auto vtxCount = reader.ReadUInt16();
        auto largestDistToOrigin = reader.ReadUInt16();

        YAML::Node vtx;
        vtx["type"] = "VTX";
        vtx["count"] = vtxCount;
        vtx["offset"] = modelOffset + vertexSetupOffset + VTX_HEADER_SIZE;
        vtx["symbol"] = symbol + "_VTX";
        Companion::Instance->AddAsset(vtx);
    }

    if (displayListSetupOffset != 0) {
        reader.Seek(modelOffset + displayListSetupOffset, LUS::SeekOffsetType::Start);
        auto dlCount = reader.ReadUInt32();
        auto unkDLInfo = reader.ReadUInt32(); // checksum?

        std::set<uint32_t> dlOffsets;
        uint32_t dlOffset = 0;
        if (dlCount > 0) {
            dlOffsets.emplace(dlOffset);
        }
        while (dlOffset < dlCount * GFX_CMD_SIZE) {
            auto w0 = reader.ReadUInt32();
            auto w1 = reader.ReadUInt32();
            dlOffset += GFX_CMD_SIZE;
            uint8_t opCode = w0 >> 24;
            
            if (opCode == GBI(G_ENDDL) && dlOffset != dlCount * GFX_CMD_SIZE) {
                dlOffsets.emplace(dlOffset);
            }
        }

        uint32_t count = 0;
        for (const auto& extractOffset : dlOffsets) {
            YAML::Node gfxNode;
            gfxNode["type"] = "GFX";
            gfxNode["offset"] = modelOffset + displayListSetupOffset + GFX_HEADER_SIZE + extractOffset;
            gfxNode["symbol"] = symbol + "_GFX_" + std::to_string(count);
            Companion::Instance->AddAsset(gfxNode);
            count++;
        }
    }

    if (unkHitboxInfoOffset != 0) {
        reader.Seek(modelOffset + unkHitboxInfoOffset, LUS::SeekOffsetType::Start);
        auto count1 = reader.ReadInt16();
        auto count2 = reader.ReadInt16();
        auto count3 = reader.ReadInt16();
        auto unk6 = reader.ReadInt16();

        for (int16_t i = 0; i < count1; i++) {
            auto xScale1 = reader.ReadInt16();
            auto yScale1 = reader.ReadInt16();
            auto zScale1 = reader.ReadInt16();
            auto xScale2 = reader.ReadInt16();
            auto yScale2 = reader.ReadInt16();
            auto zScale2 = reader.ReadInt16();
            auto xPos = reader.ReadInt16();
            auto yPos = reader.ReadInt16();
            auto zPos = reader.ReadInt16();
            auto xRot = reader.ReadUByte();
            auto yRot = reader.ReadUByte();
            auto zRot = reader.ReadUByte();
            auto unk15 = reader.ReadUByte();
            auto animIndex = reader.ReadUByte();
            reader.ReadUByte(); // pad
        }

        for (int16_t i = 0; i < count2; i++) {
            auto unk0 = reader.ReadInt16();
            auto unk2 = reader.ReadInt16();
            auto xPos = reader.ReadInt16();
            auto yPos = reader.ReadInt16();
            auto zPos = reader.ReadInt16();
            auto xRot = reader.ReadUByte();
            auto yRot = reader.ReadUByte();
            auto zRot = reader.ReadUByte();
            auto unkD = reader.ReadUByte();
            auto animIndex = reader.ReadUByte();
            reader.ReadUByte(); // pad
        }

        for (int16_t i = 0; i < count3; i++) {
            auto unk0 = reader.ReadInt16();
            auto xUnk2 = reader.ReadInt16();
            auto yUnk2 = reader.ReadInt16();
            auto zUnk2 = reader.ReadInt16();
            auto unk8 = reader.ReadUByte();
            auto animIndex = reader.ReadUByte();
            reader.ReadUByte(); // pad
        }
    }

    if (animationSetupOffset != 0) {
        reader.Seek(modelOffset + animationSetupOffset, LUS::SeekOffsetType::Start);
        auto scalingFactor = reader.ReadFloat();
        auto boneCount = reader.ReadUInt16();
        reader.ReadUInt16(); // pad

        std::vector<BoneData> bones;
        for (uint16_t i = 0; i < boneCount; i++) {
            BoneData bone;
            bone.x = reader.ReadFloat();
            bone.y = reader.ReadFloat();
            bone.z = reader.ReadFloat();
            bone.id = reader.ReadUInt16();
            bone.parentId = reader.ReadUInt16();
            bones.push_back(bone);
        }
    }

    if (collisionSetupOffset != 0) {
        reader.Seek(modelOffset + collisionSetupOffset, LUS::SeekOffsetType::Start);

        auto minIndexX = reader.ReadInt16();
        auto minIndexY = reader.ReadInt16();
        auto minIndexZ = reader.ReadInt16();
        auto maxIndexX = reader.ReadInt16();
        auto maxIndexY = reader.ReadInt16();
        auto maxIndexZ = reader.ReadInt16();
        auto yStride = reader.ReadUInt16();
        auto zStride = reader.ReadUInt16();
        auto geoCubeCount = reader.ReadUInt16();
        auto geoCubeScale = reader.ReadUInt16();
        auto triCount = reader.ReadUInt16();
        reader.ReadUInt16(); // pad

        std::vector<GeoCube> cubes;
        for (uint16_t i = 0; i < geoCubeCount; i++) {
            GeoCube cube;
            cube.startTri = reader.ReadUInt16();
            cube.triCount = reader.ReadUInt16();
            cubes.push_back(cube);
        }

        std::vector<CollisionTri> tris;
        for (uint16_t i = 0; i < triCount; i++) {
            CollisionTri tri;

            tri.vtxId1 = reader.ReadUInt16();
            tri.vtxId2 = reader.ReadUInt16();
            tri.vtxId3 = reader.ReadUInt16();
            tri.unk6 = reader.ReadUInt16();
            tri.flags = reader.ReadUInt32();
            tris.push_back(tri);
        }
    }

    if (modelUnk20Offset != 0) {
        reader.Seek(modelOffset + modelUnk20Offset, LUS::SeekOffsetType::Start);
        auto count = reader.ReadInt8();
        reader.ReadInt8(); // pad

        for (int8_t i = 0; i < count; i++) {
            auto xUnk0 = reader.ReadInt16();
            auto yUnk0 = reader.ReadInt16();
            auto zUnk0 = reader.ReadInt16();
            auto xUnk6 = reader.ReadInt16();
            auto yUnk6 = reader.ReadInt16();
            auto zUnk6 = reader.ReadInt16();
            auto unkC = reader.ReadUByte();
            reader.ReadUByte(); // pad
        }
    }

    if (effectsSetupOffset != 0) {
        reader.Seek(modelOffset + effectsSetupOffset, LUS::SeekOffsetType::Start);
        auto effectCount = reader.ReadUInt16();

        std::vector<Effect> effects;
        for (uint16_t i = 0; i < effectCount; i++) {
            Effect effect;
            effect.dataInfo = reader.ReadUInt16();
            auto vtxCount = reader.ReadUInt16();
            for (uint16_t j = 0; j < vtxCount; j++) {
                effect.vtxIndices.push_back(reader.ReadUInt16());
            }
            effects.push_back(effect);
        }
    }

    if (modelUnk28Offset != 0) {
        SPDLOG_INFO("HAS UNK 28");
        reader.Seek(modelOffset + modelUnk20Offset, LUS::SeekOffsetType::Start);
        auto count = reader.ReadInt16();
        reader.ReadInt16(); // pad

        for (int16_t i = 0; i < count; i++) {
            auto xCoord = reader.ReadInt16();
            auto yCoord = reader.ReadInt16();
            auto zCoord = reader.ReadInt16();
            auto animIndex = reader.ReadInt8();
            auto vtxCount = reader.ReadInt8();
            for (int16_t j = 0; j < vtxCount; j++) {
                auto vtxIndex = reader.ReadInt16();
            }
        }
    }

    if (animatedTextureOffset != 0) {
        reader.Seek(modelOffset + animatedTextureOffset, LUS::SeekOffsetType::Start);
        std::vector<AnimTexture> animTextureList;
        for (uint32_t i = 0; i < ANIM_TEXTURE_LIST_COUNT; i++) {
            AnimTexture animTexture;
            animTexture.frameSize = reader.ReadUInt16();
            animTexture.frameCount = reader.ReadUInt16();
            animTexture.frameRate = reader.ReadFloat();

            // Set Segment to frame 0 texture
            if (animTexture.frameSize != 0) {
                Companion::Instance->SetCompressedSegment(15 - i, fileOffset, modelOffset + modelOffset + textureSetupOffset + TEXTURE_HEADER_SIZE + textureCount * TEXTURE_METADATA_SIZE);
            }
        }
    }

    return std::make_shared<ModelData>();
}

} // namespace BK64
