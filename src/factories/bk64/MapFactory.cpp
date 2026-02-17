#include "MapFactory.h"

#include <iomanip>
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
        
        // In OTR mode, also export references for NodeProp and Prop arrays
        // Use parent's directory as base path
        std::string parentPath = *replacement;
        size_t lastSlash = parentPath.find_last_of('/');
        std::string dirPath = (lastSlash != std::string::npos) ? parentPath.substr(0, lastSlash) + "/" : "";
        
        for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
            const auto& cube = map->mCubes[cubeIdx];
            
            if (!cube.nodeProps.empty()) {
                write << "static const ALIGN_ASSET(2) char " << symbol << "_Cube" << cubeIdx << "_NodeProps[] = \"__OTR__" << dirPath << symbol << "_Cube" << cubeIdx << "_NodeProps\";\n";
            }
            
            if (!cube.props.empty()) {
                write << "static const ALIGN_ASSET(2) char " << symbol << "_Cube" << cubeIdx << "_Props[] = \"__OTR__" << dirPath << symbol << "_Cube" << cubeIdx << "_Props\";\n";
            }
        }
        write << "\n";
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
            
            if (!cube.nodeProps.empty()) {
                write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ (NodeProp*)" << symbol << "_Cube" << cubeIdx << "_NodeProps,\n";
            } else {
                write << fourSpaceTab << fourSpaceTab << "/* prop1Ptr */ NULL,\n";
            }
            
            if (!cube.props.empty()) {
                write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ (Prop*)" << symbol << "_Cube" << cubeIdx << "_Props\n";
            } else {
                write << fourSpaceTab << fourSpaceTab << "/* prop2Ptr */ NULL\n";
            }
            
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
                    // ActorProps are runtime-only, shouldn't be in exported code
                    SPDLOG_WARN("Attempting to export ActorProp in code - this is runtime data only!");
                    write << "/* ActorProp - runtime only */";
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
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    // Export NodeProps and Props as separate assets first
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        
        // Export NodeProps array as separate resource
        if (!cube.nodeProps.empty()) {
            YAML::Node nodePropAsset;
            nodePropAsset["type"] = "BK64:NODEPROP_ARRAY";
            nodePropAsset["symbol"] = symbol + "_Cube" + std::to_string(cubeIdx) + "_NodeProps";
            nodePropAsset["cubeIndex"] = cubeIdx;
            nodePropAsset["parentSymbol"] = symbol;
            Companion::Instance->AddAsset(nodePropAsset);
        }
        
        // Export Props array as separate resource
        if (!cube.props.empty()) {
            YAML::Node propAsset;
            propAsset["type"] = "BK64:PROP_ARRAY";
            propAsset["symbol"] = symbol + "_Cube" + std::to_string(cubeIdx) + "_Props";
            propAsset["cubeIndex"] = cubeIdx;
            propAsset["parentSymbol"] = symbol;
            Companion::Instance->AddAsset(propAsset);
        }
    }

    WriteHeader(writer, Torch::ResourceType::BKMap, 0);
    
    writer.Write((uint32_t)map->mCubes.size());
    
    // Extract parent directory from replacement path for child asset references
    std::string parentPath = *replacement;
    size_t lastSlash = parentPath.find_last_of('/');
    std::string dirPath = (lastSlash != std::string::npos) ? parentPath.substr(0, lastSlash) + "/" : "";
    
    // Write cube headers and resource references
    for (size_t cubeIdx = 0; cubeIdx < map->mCubes.size(); cubeIdx++) {
        const auto& cube = map->mCubes[cubeIdx];
        
        // Pack cube header (position, counts, boundary)
        uint32_t cubeHeader = ((cube.x & 0x1F) << 27) |
                              ((cube.y & 0x1F) << 22) |
                              ((cube.z & 0x1F) << 17) |
                              ((cube.prop1Cnt & 0x3F) << 11) |
                              ((cube.prop2Cnt & 0x3F) << 5)  |
                              ((cube.unk0_4 & 0x1F) << 0);
        writer.Write(cubeHeader);

        // Write NodeProp array resource reference (hash)
        if (!cube.nodeProps.empty()) {
            std::string nodePropPath = dirPath + symbol + "_Cube" + std::to_string(cubeIdx) + "_NodeProps";
            uint64_t hash = CRC64(nodePropPath.c_str());
            SPDLOG_INFO("Cube {} NodeProps reference: path={} hash=0x{:X}", cubeIdx, nodePropPath, hash);
            writer.Write(hash);
        } else {
            writer.Write((uint64_t)0);  // NULL reference
        }
        
        // Write Prop array resource reference (hash)
        if (!cube.props.empty()) {
            std::string propPath = dirPath + symbol + "_Cube" + std::to_string(cubeIdx) + "_Props";
            uint64_t hash = CRC64(propPath.c_str());
            SPDLOG_INFO("Cube {} Props reference: path={} hash=0x{:X}", cubeIdx, propPath, hash);
            writer.Write(hash);
        } else {
            writer.Write((uint64_t)0);  // NULL reference
        }
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
                const uint8_t flags = prop.raw[10];
                const char* typeName = GetPropTypeName(flags);
                bool is_visible = (flags & PROP_FLAG_VISIBLE) != 0;
                
                out << YAML::BeginMap;
                out << YAML::Key << "Type" << YAML::Value << typeName;
                
                if (flags & PROP_FLAG_ACTOR) {
                    // ActorProps are runtime-only, skip in modding export
                    out << YAML::Comment("ActorProp - runtime only, not in ROM");
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
                SPDLOG_WARN("Unknown chunk type 0x{:02X} at offset 0x{:X} in asset {}", 
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

// Helper: Parse cube data section (chunk type 0x01)
static void ParseCubeSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                             size_t totalSize, const std::string& symbol) 
{
    // Read grid bounds
    map->mCubeMin[0] = reader.ReadInt32();
    map->mCubeMin[1] = reader.ReadInt32();
    map->mCubeMin[2] = reader.ReadInt32();
    map->mCubeMax[0] = reader.ReadInt32();
    map->mCubeMax[1] = reader.ReadInt32();
    map->mCubeMax[2] = reader.ReadInt32();

    // Parse cubes until we hit a chunk marker or end of expected data
    while (reader.GetBaseAddress() + 4 <= totalSize) {
        // Peek ahead to check if next byte is a chunk marker (< 0x10)
        uint8_t possibleMarker = reader.GetBuffer()[reader.GetBaseAddress()];
        if (possibleMarker < 0x10 && reader.GetBaseAddress() + 4 < totalSize) {
            // Likely a new chunk marker, stop parsing cubes
            break;
        }

        CubeData cube;
            uint32_t raw = reader.ReadUInt32();

            cube.x        = (raw >> 27) & 0x1F;
            cube.y        = (raw >> 22) & 0x1F;
            cube.z        = (raw >> 17) & 0x1F;
            cube.prop1Cnt = (raw >> 11) & 0x3F;
            cube.prop2Cnt = (raw >>  5) & 0x3F;
            cube.unk0_4   = (raw >>  0) & 0x1F;

            // Validate the counts from cube header against remaining data
            const size_t requiredBytes = (cube.prop1Cnt * 20) + (cube.prop2Cnt * 12);
            const size_t remainingBytes = decodedData.size() - reader.GetBaseAddress();
            
            if (requiredBytes > remainingBytes) {
                SPDLOG_WARN("Cube {} header at offset 0x{:X} specifies {} NodeProps + {} Props (needs {} bytes) but only {} bytes remain in asset {}", 
                            cubes.size(), reader.GetBaseAddress() - 4, 
                            (uint32_t)cube.prop1Cnt, (uint32_t)cube.prop2Cnt, requiredBytes, remainingBytes, symbol);
                break;
            }

            // Parse NodeProps
            for (uint32_t j = 0; j < cube.prop1Cnt; j++) {
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
            }

            // Parse Props (decode union types based on flags)
            // Note: ROM data should ONLY contain ModelProp and SpriteProp
            // ActorProps are created dynamically at runtime when actors spawn
            for (uint32_t j = 0; j < cube.prop2Cnt; j++) {
                // Peek at flags byte to determine type
                const size_t currentPos = reader.GetBaseAddress();
                reader.Seek(currentPos + 10, LUS::SeekOffsetType::Start);
                uint16_t flagsWord = reader.ReadUInt16();
                reader.Seek(currentPos, LUS::SeekOffsetType::Start);
                
                uint8_t flags = (flagsWord & 0xFF);  // Lower byte contains is_actor and is_3d

                Prop prop;
                if (flags & PROP_FLAG_ACTOR) {
                    // ActorProps should NOT exist in ROM - skip with warning
                    SPDLOG_WARN("Found ActorProp in ROM data for {} cube {} prop {} - ActorProps are runtime-only! Skipping.", 
                                symbol, cubes.size(), j);
                    // Skip the 12 bytes
                    reader.Seek(currentPos + 12, LUS::SeekOffsetType::Start);
                    continue;  // Don't add to props vector
                } else if (flags & PROP_FLAG_3D) {
                    // ModelProp: unk0(2bytes), yaw(1), roll(1), position[3](6bytes), scale(1), flags(1)
                    prop.model.unk0 = reader.ReadUInt16();
                    prop.model.yaw = reader.ReadUByte();
                    prop.model.roll = reader.ReadUByte();
                    prop.model.position[0] = reader.ReadInt16();
                    prop.model.position[1] = reader.ReadInt16();
                    prop.model.position[2] = reader.ReadInt16();
                    prop.model.scale = reader.ReadUByte();
                    prop.model.flags = reader.ReadUByte();
                } else {
                    // SpriteProp: word0(4bytes), unk4[3](6bytes), wordA(2bytes)
                    prop.sprite.word0 = reader.ReadUInt32();
                    prop.sprite.unk4[0] = reader.ReadInt16();
                    prop.sprite.unk4[1] = reader.ReadInt16();
                    prop.sprite.unk4[2] = reader.ReadInt16();
                    prop.sprite.wordA = reader.ReadUInt16();
                }
                cube.props.push_back(prop);
            }

            map->mCubes.push_back(cube);
        }
}

// Helper: Parse camera node section (chunk type 0x03)
static void ParseCameraSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                               const std::string& symbol)
{
    // Camera nodes format: marker 0x01 + index (s16) + type marker 0x02 + type (u8) + type-specific data
    // Loop until marker 0x00 or another chunk marker
    while (reader.GetBaseAddress() + 1 < reader.GetLength()) {
        uint8_t marker = reader.ReadUByte();
        
        if (marker == 0x00) {
            // End of camera section
            break;
        } else if (marker == 0x01) {
            CameraNode node;
            node.index = reader.ReadInt16();
            
            // Read type marker and camera node type
            uint8_t typeMarker = reader.ReadUByte();
            if (typeMarker != 0x02) {
                SPDLOG_WARN("Expected type marker 0x02, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            typeMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            
            node.type = reader.ReadUByte();
            
            // Parse type-specific data based on camera node type
            switch (node.type) {
                case 1: // CameraNodeType1 - Scripted/path camera for cutscenes (44 bytes)
                    node.data.type1.position[0] = reader.ReadFloat();
                    node.data.type1.position[1] = reader.ReadFloat();
                    node.data.type1.position[2] = reader.ReadFloat();
                    node.data.type1.horizontalSpeed = reader.ReadFloat();
                    node.data.type1.verticalSpeed = reader.ReadFloat();
                    node.data.type1.rotation = reader.ReadFloat();
                    node.data.type1.accelaration = reader.ReadFloat();
                    node.data.type1.pitchYawRoll[0] = reader.ReadFloat();
                    node.data.type1.pitchYawRoll[1] = reader.ReadFloat();
                    node.data.type1.pitchYawRoll[2] = reader.ReadFloat();
                    node.data.type1.unknownFlag = reader.ReadInt32();
                    break;
                    
                case 2: // CameraNodeType2 - Dynamic camera (24 bytes)
                    node.data.type2.position[0] = reader.ReadFloat();
                    node.data.type2.position[1] = reader.ReadFloat();
                    node.data.type2.position[2] = reader.ReadFloat();
                    node.data.type2.pitchYawRoll[0] = reader.ReadFloat();
                    node.data.type2.pitchYawRoll[1] = reader.ReadFloat();
                    node.data.type2.pitchYawRoll[2] = reader.ReadFloat();
                    break;
                    
                case 3: // CameraNodeType3 - Static camera (52 bytes)
                    node.data.type3.position[0] = reader.ReadFloat();
                    node.data.type3.position[1] = reader.ReadFloat();
                    node.data.type3.position[2] = reader.ReadFloat();
                    node.data.type3.horizontalSpeed = reader.ReadFloat();
                    node.data.type3.verticalSpeed = reader.ReadFloat();
                    node.data.type3.rotation = reader.ReadFloat();
                    node.data.type3.accelaration = reader.ReadFloat();
                    node.data.type3.closeDistance = reader.ReadFloat();
                    node.data.type3.farDistance = reader.ReadFloat();
                    node.data.type3.pitchYawRoll[0] = reader.ReadFloat();
                    node.data.type3.pitchYawRoll[1] = reader.ReadFloat();
                    node.data.type3.pitchYawRoll[2] = reader.ReadFloat();
                    node.data.type3.unknownFlag = reader.ReadInt32();
                    break;
                    
                case 4: // CameraNodeType4 - Random camera (4 bytes)
                    node.data.type4.unknownFlag = reader.ReadInt32();
                    break;
                    
                default:
                    SPDLOG_WARN("Unknown camera node type {} at offset 0x{:X} in asset {}", 
                                node.type, reader.GetBaseAddress() - 1, symbol);
                    break;
            }
            
            map->mCameraNodes.push_back(node);
        } else {
            SPDLOG_WARN("Unexpected marker 0x{:02X} in camera section at offset 0x{:X} in asset {}", 
                        marker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }
}

// Helper: Parse lighting vector section (chunk type 0x04)
static void ParseLightingSection(LUS::BinaryReader& reader, std::shared_ptr<MapData>& map,
                                 const std::string& symbol)
{
    // Lighting format: marker 0x01 + position (f32[3]) + fade_radii (f32[2]) + rgb (s32[3])
    // Loop until marker 0x00 or another chunk marker
    while (reader.GetBaseAddress() + 1 < reader.GetLength()) {
        uint8_t marker = reader.ReadUByte();
        
        if (marker == 0x00) {
            // End of lighting section
            break;
        } else if (marker == 0x01) {
            LightingVector light;
            
            // Read position marker + data
            uint8_t posMarker = reader.ReadUByte();
            if (posMarker != 0x02) {
                SPDLOG_WARN("Expected position marker 0x02, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            posMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.position[0] = reader.ReadFloat();
            light.position[1] = reader.ReadFloat();
            light.position[2] = reader.ReadFloat();
            
            // Read fade radii marker + data
            uint8_t fadeMarker = reader.ReadUByte();
            if (fadeMarker != 0x03) {
                SPDLOG_WARN("Expected fade marker 0x03, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            fadeMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.fadeRadii[0] = reader.ReadFloat();
            light.fadeRadii[1] = reader.ReadFloat();
            
            // Read RGB marker + data
            uint8_t rgbMarker = reader.ReadUByte();
            if (rgbMarker != 0x04) {
                SPDLOG_WARN("Expected RGB marker 0x04, got 0x{:02X} at offset 0x{:X} in asset {}", 
                            rgbMarker, reader.GetBaseAddress() - 1, symbol);
                break;
            }
            light.rgb[0] = reader.ReadInt32();
            light.rgb[1] = reader.ReadInt32();
            light.rgb[2] = reader.ReadInt32();
            
            map->mLightingVectors.push_back(light);
        } else {
            SPDLOG_WARN("Unexpected marker 0x{:02X} in lighting section at offset 0x{:X} in asset {}", 
                        marker, reader.GetBaseAddress() - 1, symbol);
            break;
        }
    }
}

} // namespace BK64
