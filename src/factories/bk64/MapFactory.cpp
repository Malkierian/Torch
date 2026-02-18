#include "MapFactory.h"

#include <iomanip>
#include <sstream>
#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

namespace BK64 {

// Prop type lookup tables
static const std::unordered_map<uint8_t, std::string> sPropTypeNames = {
    { 0x0, "Sprite" },  // is_actor=0, is_3d=0
    { 0x1, "Actor" },   // is_actor=1
    { 0x2, "Model" },   // is_actor=0, is_3d=1
};

// NodeProp category lookup (bit6 field)
static const std::unordered_map<uint8_t, std::string> sNodePropCategories = {
    { 0x6, "ActorSpawn" },
    { 0x7, "Warp" },
    { 0x9, "Trigger" },
    { 0xA, "Event" },
};

// Forward declarations for chunk parsers
static void ParseCubeSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                             size_t totalSize, const std::string& symbol);
static void ParseCameraSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                               const std::string& symbol);
static void ParseLightingSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                                 const std::string& symbol);


// Flag bit masks
static constexpr uint8_t PROP_FLAG_ACTOR    = 0x01;  // is_actor bit
static constexpr uint8_t PROP_FLAG_3D       = 0x02;  // is_3d bit
static constexpr uint8_t PROP_FLAG_VISIBLE  = 0x10;  // visibility bit
static constexpr uint8_t PROP_FLAG_COLLISION = 0x20; // collision bit (ModelProps)

// Helper to determine prop type from flags
static inline uint8_t GetPropType(uint8_t flags) {
    if (flags & PROP_FLAG_ACTOR) return 0x1;  // Actor
    if (flags & PROP_FLAG_3D) return 0x2;     // Model
    return 0x0;                               // Sprite
}

static inline const char* GetPropTypeName(uint8_t flags) {
    auto type = GetPropType(flags);
    auto it = sPropTypeNames.find(type);
    return (it != sPropTypeNames.end()) ? it->second.c_str() : "Unknown";
}

static inline const char* GetNodePropCategoryName(uint8_t bit6) {
    auto it = sNodePropCategories.find(bit6);
    return (it != sNodePropCategories.end()) ? it->second.c_str() : "Unknown";
}

ExportResult MapHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto map = std::static_pointer_cast<MapData>(raw);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    // Export NodeProp and Prop array declarations for each cube
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        
        if (!cube.nodeProps.empty()) {
            write << "extern NodeProp " << symbol << "_Cube" << cubeIdx << "_NodeProps[" << cube.nodeProps.size() << "];\n";
        }
        
        if (!cube.props.empty()) {
            write << "extern Prop " << symbol << "_Cube" << cubeIdx << "_Props[" << cube.props.size() << "];\n";
        }
    }
    
    write << "extern Cube " << symbol << "[" << map->mCubes.size() << "];\n";
    return std::nullopt;
}

ExportResult MapCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto map = std::static_pointer_cast<MapData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    // In OTR mode, export cube array with __OTR__ references
    if (Companion::Instance->IsOTRMode()) {
        write << "Cube " << symbol << "[] = {\n";
        for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
            const auto& cube = map->mCubes[cubeIdx];
            write << fourSpaceTab << "{\n";
            write << fourSpaceTab << fourSpaceTab << "/* coord */ " << cube.x << ", " << cube.y << ", " << cube.z << ",\n";
            write << fourSpaceTab << fourSpaceTab << "/* prop1Cnt */ " << cube.prop1Cnt << ", /* prop2Cnt */ " << cube.prop2Cnt << ",\n";
            write << fourSpaceTab << fourSpaceTab << "/* unk0_4 */ " << cube.unk0_4 << ",\n";
            
            write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ NULL,\n";
            write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ NULL\n";
            
            write << fourSpaceTab << "},\n";
        }
        write << "};\n\n";
        return offset;
    }

    // Export NodeProps for each cube
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        
        if (!cube.nodeProps.empty()) {
            write << "NodeProp " << symbol << "_Cube" << cubeIdx << "_NodeProps[] = {\n";
            for (const auto& nodeProp : cube.nodeProps) {
                write << fourSpaceTab << "{\n";
                write << fourSpaceTab << fourSpaceTab << "/* pos */ " << nodeProp.position[0] << ", " << nodeProp.position[1] << ", " << nodeProp.position[2] << ",\n";
                write << fourSpaceTab << fourSpaceTab << "/* radius */ " << nodeProp.radius << ",\n";
                write << fourSpaceTab << fourSpaceTab << "/* type */ " << (uint32_t)nodeProp.bit6;
                
                if (nodeProp.bit6 == 6) {
                    write << ", /* actor */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
                } else if (nodeProp.bit6 == 7) {
                    write << ", /* warp */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
                } else if (nodeProp.bit6 == 9) {
                    write << ", /* trigger */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
                } else if (nodeProp.bit6 == 0xA) {
                    write << ", /* event */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
                } else {
                    write << ", " << std::hex << "0x" << nodeProp.unk8 << std::dec;
                }
                
                write << ",\n";
                write << fourSpaceTab << fourSpaceTab << "/* yaw */ " << nodeProp.yaw << ", /* scale */ " << nodeProp.scale << "\n";
                write << fourSpaceTab << "},\n";
            }
            write << "};\n\n";
        }

        if (!cube.props.empty()) {
            write << "Prop " << symbol << "_Cube" << cubeIdx << "_Props[] = {\n";
            for (const auto& prop : cube.props) {
                const uint8_t flags = prop.raw[10];
                const char* typeName = GetPropTypeName(flags);
                
                write << fourSpaceTab << "{ ." << typeName << " = { ";
                
                if (flags & PROP_FLAG_ACTOR) {
                    // ActorProp: marker is NULL in ROM (set at runtime), position and flags are valid
                    write << "NULL, { "
                          << prop.actor.position[0] << ", "
                          << prop.actor.position[1] << ", "
                          << prop.actor.position[2] << " }, ";
                    write << std::hex << "0x" << prop.actor.flags << std::dec;
                } else if (flags & PROP_FLAG_3D) {
                    // ModelProp: unk0(2), yaw(1), roll(1), position[3](6), scale(1), flags(1)
                    write << std::hex << "0x" << prop.model.unk0 << std::dec << ", ";
                    write << (int)prop.model.yaw << ", " << (int)prop.model.roll << ", ";
                    write << "{ " << prop.model.position[0] << ", " 
                          << prop.model.position[1] << ", " 
                          << prop.model.position[2] << " }, ";
                    write << (int)prop.model.scale << ", ";
                    write << std::hex << "0x" << (int)prop.model.flags << std::dec;
                } else {
                    // SpriteProp: word0(4), unk4[3](6), wordA(2)
                    write << std::hex << "0x" << prop.sprite.word0 << std::dec << ", ";
                    write << "{ " << prop.sprite.unk4[0] << ", "
                          << prop.sprite.unk4[1] << ", "
                          << prop.sprite.unk4[2] << " }, ";
                    write << std::hex << "0x" << prop.sprite.wordA << std::dec;
                }
                
                write << " } },\n";
            }
            write << "};\n\n";
        }
    }

    // Export cube array
    write << "Cube " << symbol << "[] = {\n";
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        write << fourSpaceTab << "{\n";
        write << fourSpaceTab << fourSpaceTab << "/* coord */ " << cube.x << ", " << cube.y << ", " << cube.z << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* prop1Cnt */ " << cube.prop1Cnt << ", /* prop2Cnt */ " << cube.prop2Cnt << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* unk0_4 */ " << cube.unk0_4 << ",\n";
        
        if (!cube.nodeProps.empty()) {
            write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ " << symbol << "_Cube" << cubeIdx << "_NodeProps,\n";
        } else {
            write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ NULL,\n";
        }
        
        if (!cube.props.empty()) {
            write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ " << symbol << "_Cube" << cubeIdx << "_Props\n";
        } else {
            write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ NULL\n";
        }
        
        write << fourSpaceTab << "},\n";
    }
    write << "};\n\n";

    return offset;
}

ExportResult MapBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto map = std::static_pointer_cast<MapData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKMap, 0);

    // --- Cube section ---
    writer.Write((uint32_t)map->mCubes.size());
    writer.Write(map->mCubeMin[0]);
    writer.Write(map->mCubeMin[1]);
    writer.Write(map->mCubeMin[2]);
    writer.Write(map->mCubeMax[0]);
    writer.Write(map->mCubeMax[1]);
    writer.Write(map->mCubeMax[2]);

    for (const auto& cube : map->mCubes) {
        uint32_t cubeHeader = ((cube.x        & 0x1F) << 27) |
                              ((cube.y        & 0x1F) << 22) |
                              ((cube.z        & 0x1F) << 17) |
                              ((cube.prop1Cnt & 0x3F) << 11) |
                              ((cube.prop2Cnt & 0x3F) <<  5) |
                              ((cube.unk0_4   & 0x1F) <<  0);
        writer.Write(cubeHeader);

        // NodeProps inline
        writer.Write((uint32_t)cube.nodeProps.size());
        for (const auto& np : cube.nodeProps) {
            writer.Write(np.position[0]);
            writer.Write(np.position[1]);
            writer.Write(np.position[2]);
            uint16_t f1 = ((np.radius & 0x1FF) << 7) |
                          ((np.bit6   & 0x3F)  << 1) |
                          ((np.bit0   & 0x01)  << 0);
            writer.Write(f1);
            writer.Write(np.unk8);
            writer.Write(np.unkA);
            writer.Write(np.padB);
            uint32_t f2 = ((np.yaw   & 0x1FF)     << 23) |
                          ((np.scale & 0x7FFFFF)   <<  0);
            writer.Write(f2);
            uint32_t f3 = ((np.unk10_31 & 0xFFF) << 20) |
                          ((np.unk10_19 & 0xFFF) <<  8) |
                          ((np.pad10_7  & 0x01)  <<  7) |
                          ((np.unk10_6  & 0x01)  <<  6) |
                          ((np.pad10_5  & 0x0F)  <<  2) |
                          ((np.unk10_0  & 0x03)  <<  0);
            writer.Write(f3);
        }

        // Props inline (raw 12-byte structs)
        writer.Write((uint32_t)cube.props.size());
        for (const auto& prop : cube.props) {
            writer.Write((char*)prop.raw, 12);
        }
    }

    // --- Camera section ---
    writer.Write((uint32_t)map->mCameraNodes.size());
    for (const auto& cam : map->mCameraNodes) {
        writer.Write(cam.index);
        writer.Write(cam.type);
        switch (cam.type) {
            case 1:
                writer.Write(cam.data.type1.position[0]);
                writer.Write(cam.data.type1.position[1]);
                writer.Write(cam.data.type1.position[2]);
                writer.Write(cam.data.type1.horizontalSpeed);
                writer.Write(cam.data.type1.verticalSpeed);
                writer.Write(cam.data.type1.rotation);
                writer.Write(cam.data.type1.accelaration);
                writer.Write(cam.data.type1.pitchYawRoll[0]);
                writer.Write(cam.data.type1.pitchYawRoll[1]);
                writer.Write(cam.data.type1.pitchYawRoll[2]);
                writer.Write(cam.data.type1.unknownFlag);
                break;
            case 2:
                writer.Write(cam.data.type2.position[0]);
                writer.Write(cam.data.type2.position[1]);
                writer.Write(cam.data.type2.position[2]);
                writer.Write(cam.data.type2.pitchYawRoll[0]);
                writer.Write(cam.data.type2.pitchYawRoll[1]);
                writer.Write(cam.data.type2.pitchYawRoll[2]);
                break;
            case 3:
                writer.Write(cam.data.type3.position[0]);
                writer.Write(cam.data.type3.position[1]);
                writer.Write(cam.data.type3.position[2]);
                writer.Write(cam.data.type3.horizontalSpeed);
                writer.Write(cam.data.type3.verticalSpeed);
                writer.Write(cam.data.type3.rotation);
                writer.Write(cam.data.type3.accelaration);
                writer.Write(cam.data.type3.closeDistance);
                writer.Write(cam.data.type3.farDistance);
                writer.Write(cam.data.type3.pitchYawRoll[0]);
                writer.Write(cam.data.type3.pitchYawRoll[1]);
                writer.Write(cam.data.type3.pitchYawRoll[2]);
                writer.Write(cam.data.type3.unknownFlag);
                break;
            case 4:
                writer.Write(cam.data.type4.unknownFlag);
                break;
            case 0:
                // Type 0 is a valid empty node with no data fields
                break;
            default:
                SPDLOG_WARN("[BK64:MAP] Binary export: unknown camera type {}", cam.type);
                break;
        }
    }

    // --- Lighting section ---
    writer.Write((uint32_t)map->mLightingVectors.size());
    for (const auto& light : map->mLightingVectors) {
        writer.Write(light.position[0]);
        writer.Write(light.position[1]);
        writer.Write(light.position[2]);
        writer.Write(light.fadeRadii[0]);
        writer.Write(light.fadeRadii[1]);
        writer.Write(light.rgb[0]);
        writer.Write(light.rgb[1]);
        writer.Write(light.rgb[2]);
    }

    writer.Finish(write);
    return OffsetEntry{ 0 };
}

ExportResult MapModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto map = std::static_pointer_cast<MapData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "CubeCount";
    out << YAML::Value << map->mCubes.size();
    out << YAML::Key << "Cubes";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        
        out << YAML::BeginMap;
        out << YAML::Key << "Position";
        out << YAML::Value;
        out << YAML::Flow;
        out << YAML::BeginMap;
        out << YAML::Key << "X" << YAML::Value << cube.x;
        out << YAML::Key << "Y" << YAML::Value << cube.y;
        out << YAML::Key << "Z" << YAML::Value << cube.z;
        out << YAML::EndMap;
        
        out << YAML::Key << "Unknown";
        out << YAML::Value << cube.unk0_4;
        
        // Export NodeProps
        if (!cube.nodeProps.empty()) {
            out << YAML::Key << "NodeProps";
            out << YAML::Value;
            out << YAML::BeginSeq;
            
            for (const auto& nodeProp : cube.nodeProps) {
                out << YAML::BeginMap;
                out << YAML::Key << "Position";
                out << YAML::Value;
                out << YAML::Flow;
                out << YAML::BeginMap;
                out << YAML::Key << "X" << YAML::Value << nodeProp.position[0];
                out << YAML::Key << "Y" << YAML::Value << nodeProp.position[1];
                out << YAML::Key << "Z" << YAML::Value << nodeProp.position[2];
                out << YAML::EndMap;
                
                out << YAML::Key << "Radius" << YAML::Value << nodeProp.radius;
                out << YAML::Key << "Category" << YAML::Value << GetNodePropCategoryName(nodeProp.bit6);
                out << YAML::Key << "Type" << YAML::Value << (uint32_t)nodeProp.bit6;
                out << YAML::Key << "ActorID" << YAML::Value << YAML::Hex << nodeProp.unk8 << YAML::Dec;
                out << YAML::Key << "Yaw" << YAML::Value << nodeProp.yaw;
                out << YAML::Key << "Scale" << YAML::Value << nodeProp.scale;
                
                out << YAML::EndMap;
            }
            
            out << YAML::EndSeq;
        }
        
        // Export Props (union types - ModelProp, SpriteProp, or ActorProp)
        if (!cube.props.empty()) {
            out << YAML::Key << "Props";
            out << YAML::Value;
            out << YAML::BeginSeq;
            
            for (const auto& prop : cube.props) {
                // Determine type from discriminator flags at offset 0xA (byte 10)
                // is_actor = bit 0, is_3d = bit 1 of the flags byte
                const uint8_t flags = prop.raw[10];
                const char* typeName = GetPropTypeName(flags);
                bool is_visible = (flags & PROP_FLAG_VISIBLE) != 0;
                
                out << YAML::BeginMap;
                out << YAML::Key << "Type" << YAML::Value << typeName;
                
                if (flags & PROP_FLAG_ACTOR) {
                    // ActorProp: marker is NULL in ROM, position and flags are valid ROM data
                    out << YAML::Key << "Position";
                    out << YAML::Value;
                    out << YAML::Flow;
                    out << YAML::BeginMap;
                    out << YAML::Key << "X" << YAML::Value << prop.actor.position[0];
                    out << YAML::Key << "Y" << YAML::Value << prop.actor.position[1];
                    out << YAML::Key << "Z" << YAML::Value << prop.actor.position[2];
                    out << YAML::EndMap;
                    out << YAML::Key << "Flags" << YAML::Value << YAML::Hex << prop.actor.flags << YAML::Dec;
                } else if (flags & PROP_FLAG_3D) {
                    // ModelProp - static 3D model
                    uint16_t model_index = prop.model.unk0 & 0xFFF;
                    out << YAML::Key << "ModelIndex" << YAML::Value << model_index;
                    out << YAML::Key << "AssetID" << YAML::Value << YAML::Hex << (model_index + 0x2d1) << YAML::Dec;
                    out << YAML::Key << "Position";
                    out << YAML::Value;
                    out << YAML::Flow;
                    out << YAML::BeginMap;
                    out << YAML::Key << "X" << YAML::Value << prop.model.position[0];
                    out << YAML::Key << "Y" << YAML::Value << prop.model.position[1];
                    out << YAML::Key << "Z" << YAML::Value << prop.model.position[2];
                    out << YAML::EndMap;
                    out << YAML::Key << "Yaw" << YAML::Value << (int)prop.model.yaw * 2;
                    out << YAML::Key << "Roll" << YAML::Value << (int)prop.model.roll * 2;
                    out << YAML::Key << "Scale" << YAML::Value << (float)prop.model.scale / 100.0f;
                } else {
                    // SpriteProp - 2D billboard sprite
                    // Bit layout (32-bit big-endian): sprite_id[31:20], pad[19], r[18:16], g[15:13], b[12:10], scale[9:2], mirror[1], pad[0]
                    uint16_t sprite_index = (prop.sprite.word0 >> 20) & 0xFFF;
                    out << YAML::Key << "SpriteIndex" << YAML::Value << sprite_index;
                    out << YAML::Key << "AssetID" << YAML::Value << YAML::Hex << (sprite_index + 0x572) << YAML::Dec;
                    uint8_t r = (prop.sprite.word0 >> 16) & 0x7;
                    uint8_t g = (prop.sprite.word0 >> 13) & 0x7;
                    uint8_t b = (prop.sprite.word0 >> 10) & 0x7;
                    out << YAML::Key << "Color";
                    out << YAML::Value;
                    out << YAML::Flow;
                    out << YAML::BeginMap;
                    out << YAML::Key << "R" << YAML::Value << (int)r;
                    out << YAML::Key << "G" << YAML::Value << (int)g;
                    out << YAML::Key << "B" << YAML::Value << (int)b;
                    out << YAML::EndMap;
                    uint8_t scale = (prop.sprite.word0 >> 2) & 0xFF;
                    out << YAML::Key << "Scale" << YAML::Value << (float)scale / 100.0f;
                    bool mirrored = (prop.sprite.word0 >> 1) & 0x1;
                    out << YAML::Key << "Mirrored" << YAML::Value << (mirrored ? "true" : "false");
                    // Frame at bits [15:11] of wordA (16-bit big-endian at offset 0x0A)
                    uint8_t frame = (prop.sprite.wordA >> 11) & 0x1F;
                    out << YAML::Key << "Frame" << YAML::Value << (int)frame;
                }
                
                out << YAML::Key << "Visible" << YAML::Value << (is_visible ? "true" : "false");
                
                out << YAML::EndMap;
            }
            
            out << YAML::EndSeq;
        }
        
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::EndMap;
    out << YAML::EndMap;

    write << out.c_str();
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MapFactory::parse(
    std::vector<uint8_t>& buffer, YAML::Node& node) 
{
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    std::vector<uint8_t> decodedData;

    try {
        auto [_, segment] = Decompressor::AutoDecode(node, buffer);
        if (!segment.data || segment.size == 0) {
            SPDLOG_ERROR("Decompression returned null for symbol: {}", symbol);
            return std::nullopt;
        }

        decodedData.assign(segment.data, segment.data + segment.size);

        // BK64 Map File Format (after decompression):
        // Multi-chunk format with type markers (as seen in gsworld_load):
        //   Type 0x00: End of file
        //   Type 0x01: Cube data section (grid bounds + cube definitions)
        //   Type 0x02: Reserved/empty
        //   Type 0x03: Camera node section
        //   Type 0x04: Lighting vector section
        // 
        // Cube Section Format (type 0x01):
        //   - Min cube position (s32[3])
        //   - Max cube position (s32[3])
        //   - For each cube in grid: CubeHeader + NodeProps + Props
        //   - CubeHeader: 4 bytes (x:5, y:5, z:5, prop1Cnt:6, prop2Cnt:6, unk0_4:5)
        //   - NodeProp: 20 bytes each
        //   - Prop: 12 bytes each

        LUS::BinaryReader reader(decodedData.data(), decodedData.size());
        reader.SetEndianness(Torch::Endianness::Big);

        auto map = std::make_shared<MapData>();

        // Parse chunks until end marker (0x00) or end of data
        while (reader.GetBaseAddress() < decodedData.size()) {
            uint8_t chunkType = reader.ReadUByte();
            
            if (chunkType == 0x00) {
                // End of file marker
                break;
            }
            else if (chunkType == 0x01) {
                // Cube data section
                ParseCubeSection(reader, map, decodedData.size(), symbol);
            }
            else if (chunkType == 0x02) {
                // Reserved/empty chunk (from decomp)
                continue;
            }
            else if (chunkType == 0x03) {
                // Camera node section
                ParseCameraSection(reader, map, symbol);
            }
            else if (chunkType == 0x04) {
                // Lighting vector section
                ParseLightingSection(reader, map, symbol);
            }
            else {
                SPDLOG_WARN("[BK64:MAP] Unknown chunk type 0x{:02X} at offset 0x{:X} in asset {}", 
                            chunkType, reader.GetBaseAddress() - 1, symbol);
                break;
            }
        }

        return map;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("MapFactory parse error for {}: {}", symbol, e.what());
        return std::nullopt;
    }
}

// Helper: Read the inner content of a cube from file (code7AF80_initCubeFromFile equivalent).
// Called when the outer per-cube wrapper (below) encounters marker 0x03.
static void ReadCubeContent(LUS::BinaryReader& reader, CubeData& cube, const std::string& symbol) {
    // Format from decompilation (code7AF80_initCubeFromFile in code_A5BC0.c):
    //   Optional NodeProps (new):  0x0A + count (u8) + 0x0B + NodeProp data (20 bytes each)
    //   Optional NodeProps (old):  0x06 + count (u8) + 0x07 + OtherNode data (12 bytes each)
    //   Optional Props:            0x08 + count (u8) + 0x09 + Prop data (12 bytes each)
    // None of these are mandatory; a cube may have no props at all.

    // NodeProps
    size_t peekPos = reader.GetBaseAddress();
    uint8_t marker = reader.ReadUByte();
    reader.Seek(peekPos, LUS::SeekOffsetType::Start);

    if (marker == 0x0A || marker == 0x06) {
        marker = reader.ReadUByte();  // consume 0x0A or 0x06
        uint8_t nodeCount = reader.ReadUByte();

        if (nodeCount > 0) {
            uint8_t dataMarker = reader.ReadUByte();  // expect 0x0B or 0x07
            if ((marker == 0x0A && dataMarker != 0x0B) || (marker == 0x06 && dataMarker != 0x07)) {
                SPDLOG_WARN("[BK64:MAP] Expected data marker {} after 0x{:02X}, got 0x{:02X} at offset 0x{:X} in asset {}",
                            marker == 0x0A ? "0x0B" : "0x07", marker, dataMarker,
                            reader.GetBaseAddress() - 1, symbol);
            }
        }

        cube.prop1Cnt = nodeCount;
        cube.unk0_4 = nodeCount;

        for (uint32_t j = 0; j < nodeCount; j++) {
            if (marker == 0x0A) {
                NodeProp nodeProp;
                nodeProp.position[0] = reader.ReadInt16();
                nodeProp.position[1] = reader.ReadInt16();
                nodeProp.position[2] = reader.ReadInt16();

                uint16_t f1 = reader.ReadUInt16();
                nodeProp.radius = (f1 >> 7) & 0x1FF;
                nodeProp.bit6   = (f1 >> 1) & 0x3F;
                nodeProp.bit0   = (f1 >> 0) & 0x01;

                nodeProp.unk8 = reader.ReadUInt16();
                nodeProp.unkA = reader.ReadUByte();
                nodeProp.padB = reader.ReadUByte();

                uint32_t f2 = reader.ReadUInt32();
                nodeProp.yaw   = (f2 >> 23) & 0x1FF;
                nodeProp.scale = (f2 >>  0) & 0x7FFFFF;

                uint32_t f3 = reader.ReadUInt32();
                nodeProp.unk10_31 = (f3 >> 20) & 0xFFF;
                nodeProp.unk10_19 = (f3 >>  8) & 0xFFF;
                nodeProp.pad10_7  = (f3 >>  7) & 0x01;
                nodeProp.unk10_6  = (f3 >>  6) & 0x01;
                nodeProp.pad10_5  = (f3 >>  2) & 0x0F;
                nodeProp.unk10_0  = (f3 >>  0) & 0x03;

                cube.nodeProps.push_back(nodeProp);
            } else {
                // OtherNode (0x06 format): 12 bytes each, skip for now
                reader.Seek(reader.GetBaseAddress() + 12, LUS::SeekOffsetType::Start);
            }
        }
    }

    // Props
    peekPos = reader.GetBaseAddress();
    marker = reader.ReadUByte();
    reader.Seek(peekPos, LUS::SeekOffsetType::Start);

    if (marker == 0x08) {
        reader.ReadUByte();  // consume 0x08
        uint8_t propCount = reader.ReadUByte();

        if (propCount > 0) {
            uint8_t dataMarker = reader.ReadUByte();  // expect 0x09
            if (dataMarker != 0x09) {
                SPDLOG_WARN("[BK64:MAP] Expected data marker 0x09 after 0x08, got 0x{:02X} at offset 0x{:X} in asset {}",
                            dataMarker, reader.GetBaseAddress() - 1, symbol);
            }
        }

        cube.prop2Cnt = propCount;
        for (uint32_t j = 0; j < propCount; j++) {
            Prop prop;
            reader.Read((char*)prop.raw, 12);
            cube.props.push_back(prop);
        }
    }
}

// Helper: Read one cube's slot from the stream.
// The static gccube wrapper (__code7AF80_initCubeFromFile) loops until it sees 0x01,
// which acts as the per-cube terminator.  Inside the loop:
//   0x03 → actual cube content (calls code7AF80_initCubeFromFile)
//   0x00 → skip 6 words (two 3-word padding groups)
//   0x02 → skip 3 words
static void ReadCubeData(LUS::BinaryReader& reader, CubeData& cube, const std::string& symbol) {
    while (reader.GetBaseAddress() < reader.GetLength()) {
        uint8_t marker = reader.ReadUByte();

        if (marker == 0x01) {
            // Per-cube terminator — consumed, move to next grid position
            break;
        } else if (marker == 0x03) {
            // Actual cube content
            ReadCubeContent(reader, cube, symbol);
        } else if (marker == 0x00) {
            // Padding: two groups of 3 words (6 × s32 = 24 bytes)
            reader.Seek(reader.GetBaseAddress() + 24, LUS::SeekOffsetType::Start);
        } else if (marker == 0x02) {
            // Single group of 3 words
            reader.Seek(reader.GetBaseAddress() + 12, LUS::SeekOffsetType::Start);
        } else {
            SPDLOG_WARN("[BK64:MAP] Unexpected per-cube marker 0x{:02X} at offset 0x{:X} in asset {}",
                        marker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }
}

static void ParseCubeSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                             size_t totalSize, const std::string& symbol)
{
    // The cube section begins with an inner 0x01 sub-marker (separate from the chunk-type 0x01
    // already consumed by the outer loop).  Source: file_getNWords_ifExpected(fp, 1, from, 3)
    uint8_t innerMarker = reader.ReadUByte();
    if (innerMarker != 0x01) {
        SPDLOG_WARN("[BK64:MAP] Expected inner marker 0x01 at cube section start, got 0x{:02X} at offset 0x{:X} in asset {}",
                    innerMarker, reader.GetBaseAddress() - 1, symbol);
        return;
    }

    // from[0..2] then to[0..2] — no marker before to[] (file_getNWords unconditional)
    int32_t from[3], to[3];
    from[0] = reader.ReadInt32();
    from[1] = reader.ReadInt32();
    from[2] = reader.ReadInt32();
    to[0]   = reader.ReadInt32();
    to[1]   = reader.ReadInt32();
    to[2]   = reader.ReadInt32();

    map->mCubeMin[0] = from[0];
    map->mCubeMin[1] = from[1];
    map->mCubeMin[2] = from[2];
    map->mCubeMax[0] = to[0];
    map->mCubeMax[1] = to[1];
    map->mCubeMax[2] = to[2];

    int32_t countX = to[0] - from[0] + 1;
    int32_t countY = to[1] - from[1] + 1;
    int32_t countZ = to[2] - from[2] + 1;

    if (countX <= 0 || countY <= 0 || countZ <= 0) {
        SPDLOG_WARN("[BK64:MAP] Invalid cube bounds: from ({},{},{}) to ({},{},{}) in asset {}",
                    from[0], from[1], from[2], to[0], to[1], to[2], symbol);
        return;
    }

    int32_t totalCubes = countX * countY * countZ;

    SPDLOG_INFO("[BK64:MAP] {} cube section: from ({},{},{}) to ({},{},{}) = {} cubes",
                symbol, from[0], from[1], from[2], to[0], to[1], to[2], totalCubes);

    // Iteration order matches source exactly:
    //   for([0]=X) for([1]=Y) for([2]=Z)  — X is outermost, Z is innermost
    for (int32_t x = from[0]; x <= to[0]; x++) {
        for (int32_t y = from[1]; y <= to[1]; y++) {
            for (int32_t z = from[2]; z <= to[2]; z++) {
                CubeData cube;
                cube.x = x;
                cube.y = y;
                cube.z = z;
                cube.prop1Cnt = cube.prop2Cnt = cube.unk0_4 = 0;

                // ReadCubeData loops until it consumes the 0x01 per-cube terminator
                ReadCubeData(reader, cube, symbol);

                SPDLOG_INFO("[BK64:MAP] {} cube ({},{},{}) nodeProps={} props={}",
                             symbol, x, y, z, cube.nodeProps.size(), cube.props.size());

                map->mCubes.push_back(cube);
            }
        }
    }

    // Cube section ends with 0x00 (file_isNextByteExpected(fp, 0) in cubeList_fromFile)
    size_t peekPos = reader.GetBaseAddress();
    uint8_t endMarker = reader.ReadUByte();
    reader.Seek(peekPos, LUS::SeekOffsetType::Start);

    if (endMarker != 0x00) {
        SPDLOG_WARN("[BK64:MAP] Expected end marker 0x00 after {} cubes, got 0x{:02X} at offset 0x{:X} in asset {}",
                    totalCubes, endMarker, reader.GetBaseAddress(), symbol);
    } else {
        reader.ReadUByte();  // consume 0x00
    }
}

// Helper: Parse camera node section (chunk type 0x03)
static void ParseCameraSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                               const std::string& symbol)
{
    // ncCameraNodeList_fromFile format:
    //   while(next byte != 0x00):
    //     file_getShort_ifExpected(fp, 0x01, &index)  → marker 0x01 + s16
    //     file_getByte_ifExpected(fp, 0x02, &type)    → marker 0x02 + u8
    //     cameraNodeTypeN_fromFile(fp, this):
    //       inner sub-loop until 0x00, each field group prefixed by its own marker byte
    while (reader.GetBaseAddress() + 1 < reader.GetLength()) {
        // Peek to detect section terminator
        size_t peekPos = reader.GetBaseAddress();
        uint8_t peekMarker = reader.ReadUByte();
        reader.Seek(peekPos, LUS::SeekOffsetType::Start);

        if (peekMarker == 0x00) {
            reader.ReadUByte();  // consume 0x00 outer section terminator
            break;
        }

        // Read outer node-start marker (must be 0x01)
        uint8_t nodeMarker = reader.ReadUByte();
        if (nodeMarker != 0x01) {
            SPDLOG_WARN("[BK64:MAP] Expected node marker 0x01, got 0x{:02X} at offset 0x{:X} in asset {}",
                        nodeMarker, reader.GetBaseAddress() - 1, symbol);
            break;
        }

        CameraNode node;
        node.index = reader.ReadInt16();

        // Read type marker (0x02) + type byte
        uint8_t typeMarker = reader.ReadUByte();
        if (typeMarker != 0x02) {
            SPDLOG_WARN("[BK64:MAP] Expected type marker 0x02, got 0x{:02X} at offset 0x{:X} in asset {}",
                        typeMarker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
        node.type = reader.ReadUByte();

        // Parse inner sub-loop (cameraNodeTypeN_fromFile):
        // Each type uses its own set of sub-markers, all loops end at 0x00.
        // Type 0 is a valid empty node with NO inner data and NO inner 0x00 terminator.
        //
        // Type 1: 0x01→position[3]  0x02→hSpeed+vSpeed  0x03→rotation+accel
        //         0x04→pitchYawRoll[3]  0x05→unknownFlag
        // Type 2: 0x01→position[3]  0x02→pitchYawRoll[3]
        // Type 3: 0x01→position[3]  0x02→hSpeed+vSpeed  0x03→rotation+accel
        //         0x06→closeDist+farDist  0x04→pitchYawRoll[3]  0x05→unknownFlag
        // Type 4: 0x01→unknownFlag
        bool innerError = false;
        if (node.type == 0) {
            // No inner data, no terminator — proceed directly to push
        } else while (!innerError && reader.GetBaseAddress() < reader.GetLength()) {
            uint8_t sub = reader.ReadUByte();
            if (sub == 0x00) break;

            switch (node.type) {
                case 1:
                    switch (sub) {
                        case 0x01:
                            node.data.type1.position[0] = reader.ReadFloat();
                            node.data.type1.position[1] = reader.ReadFloat();
                            node.data.type1.position[2] = reader.ReadFloat();
                            break;
                        case 0x02:
                            node.data.type1.horizontalSpeed = reader.ReadFloat();
                            node.data.type1.verticalSpeed   = reader.ReadFloat();
                            break;
                        case 0x03:
                            node.data.type1.rotation     = reader.ReadFloat();
                            node.data.type1.accelaration = reader.ReadFloat();
                            break;
                        case 0x04:
                            node.data.type1.pitchYawRoll[0] = reader.ReadFloat();
                            node.data.type1.pitchYawRoll[1] = reader.ReadFloat();
                            node.data.type1.pitchYawRoll[2] = reader.ReadFloat();
                            break;
                        case 0x05:
                            node.data.type1.unknownFlag = reader.ReadInt32();
                            break;
                        default:
                            SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type1 camera node at 0x{:X} in {}",
                                        sub, reader.GetBaseAddress() - 1, symbol);
                            innerError = true;
                    }
                    break;

                case 2:
                    switch (sub) {
                        case 0x01:
                            node.data.type2.position[0] = reader.ReadFloat();
                            node.data.type2.position[1] = reader.ReadFloat();
                            node.data.type2.position[2] = reader.ReadFloat();
                            break;
                        case 0x02:
                            node.data.type2.pitchYawRoll[0] = reader.ReadFloat();
                            node.data.type2.pitchYawRoll[1] = reader.ReadFloat();
                            node.data.type2.pitchYawRoll[2] = reader.ReadFloat();
                            break;
                        default:
                            SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type2 camera node at 0x{:X} in {}",
                                        sub, reader.GetBaseAddress() - 1, symbol);
                            innerError = true;
                    }
                    break;

                case 3:
                    switch (sub) {
                        case 0x01:
                            node.data.type3.position[0] = reader.ReadFloat();
                            node.data.type3.position[1] = reader.ReadFloat();
                            node.data.type3.position[2] = reader.ReadFloat();
                            break;
                        case 0x02:
                            node.data.type3.horizontalSpeed = reader.ReadFloat();
                            node.data.type3.verticalSpeed   = reader.ReadFloat();
                            break;
                        case 0x03:
                            node.data.type3.rotation     = reader.ReadFloat();
                            node.data.type3.accelaration = reader.ReadFloat();
                            break;
                        case 0x06:
                            node.data.type3.closeDistance = reader.ReadFloat();
                            node.data.type3.farDistance   = reader.ReadFloat();
                            break;
                        case 0x04:
                            node.data.type3.pitchYawRoll[0] = reader.ReadFloat();
                            node.data.type3.pitchYawRoll[1] = reader.ReadFloat();
                            node.data.type3.pitchYawRoll[2] = reader.ReadFloat();
                            break;
                        case 0x05:
                            node.data.type3.unknownFlag = reader.ReadInt32();
                            break;
                        default:
                            SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type3 camera node at 0x{:X} in {}",
                                        sub, reader.GetBaseAddress() - 1, symbol);
                            innerError = true;
                    }
                    break;

                case 4:
                    switch (sub) {
                        case 0x01:
                            node.data.type4.unknownFlag = reader.ReadInt32();
                            break;
                        default:
                            SPDLOG_WARN("[BK64:MAP] Unknown sub-marker 0x{:02X} in type4 camera node at 0x{:X} in {}",
                                        sub, reader.GetBaseAddress() - 1, symbol);
                            innerError = true;
                    }
                    break;

                default:
                    SPDLOG_WARN("[BK64:MAP] Unknown camera node type {} at offset 0x{:X} in asset {}",
                                node.type, reader.GetBaseAddress(), symbol);
                    innerError = true;
            }
        }

        map->mCameraNodes.push_back(node);
        SPDLOG_INFO("[BK64:MAP] {} camera node index={} type={}", symbol, node.index, node.type);

        if (innerError) break;
    }
    // Camera section has consumed its own 0x00 terminator; outer loop sees the next chunk.
    SPDLOG_INFO("[BK64:MAP] {} camera section: {} nodes", symbol, map->mCameraNodes.size());
}

// Helper: Parse lighting vector section (chunk type 0x04)
static void ParseLightingSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                                 const std::string& symbol)
{
    // Lighting format: marker 0x01 + position (f32[3]) + fade_radii (f32[2]) + rgb (s32[3])
    // Loop until chunk marker (0x00-0x04) is encountered
    while (reader.GetBaseAddress() + 1 < reader.GetLength()) {
        // Peek at next byte to detect chunk markers without consuming them
        size_t peekPos = reader.GetBaseAddress();
        uint8_t peekMarker = reader.ReadUByte();
        reader.Seek(peekPos, LUS::SeekOffsetType::Start);
        
        // Lighting section ends when 0x00 is encountered (lightingVectorList_fromFile loops
        // while(!file_isNextByteExpected(fp, 0))).  0x01 is the valid entry start marker.
        if (peekMarker == 0x00) {
            reader.ReadUByte();  // consume the 0x00 section terminator
            break;
        }
        
        // Otherwise, read the actual marker and process
        uint8_t marker = reader.ReadUByte();
        
        if (marker == 0x01) {
            LightingVector light;
            
            // Read position marker + data
            uint8_t posMarker = reader.ReadUByte();
            if (posMarker != 0x02) {
                SPDLOG_WARN("[BK64:MAP] Expected position marker 0x02, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            posMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.position[0] = reader.ReadFloat();
            light.position[1] = reader.ReadFloat();
            light.position[2] = reader.ReadFloat();
            
            // Read fade radii marker + data
            uint8_t fadeMarker = reader.ReadUByte();
            if (fadeMarker != 0x03) {
                SPDLOG_WARN("[BK64:MAP] Expected fade marker 0x03, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            fadeMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.fadeRadii[0] = reader.ReadFloat();
            light.fadeRadii[1] = reader.ReadFloat();
            
            // Read RGB marker + data
            uint8_t rgbMarker = reader.ReadUByte();
            if (rgbMarker != 0x04) {
                SPDLOG_WARN("[BK64:MAP] Expected RGB marker 0x04, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            rgbMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.rgb[0] = reader.ReadInt32();
            light.rgb[1] = reader.ReadInt32();
            light.rgb[2] = reader.ReadInt32();
            
            map->mLightingVectors.push_back(light);
            SPDLOG_INFO("[BK64:MAP] {} light pos=({:.2f},{:.2f},{:.2f}) radii=({:.2f},{:.2f}) rgb=({},{},{})",
                         symbol,
                         light.position[0], light.position[1], light.position[2],
                         light.fadeRadii[0], light.fadeRadii[1],
                         light.rgb[0], light.rgb[1], light.rgb[2]);
        } else {
            SPDLOG_WARN("[BK64:MAP] Unexpected marker 0x{:02X} in lighting section at offset 0x{:X} in asset {}", 
                        marker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }
    // Lighting section has consumed its own 0x00 terminator; outer loop sees the next chunk.
    SPDLOG_INFO("[BK64:MAP] {} lighting section: {} vectors", symbol, map->mLightingVectors.size());
}

} // namespace BK64
