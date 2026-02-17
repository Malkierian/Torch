#include "LevelSetupFactory.h"

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

ExportResult LevelSetupHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto levelSetup = std::static_pointer_cast<LevelSetupData>(raw);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    // Export NodeProp and Prop array declarations for each cube
    for (size_t cubeIdx = 0; cubeIdx < levelSetup->mCubes.size(); cubeIdx++) {
        const auto& cube = levelSetup->mCubes[cubeIdx];
        
        if (!cube.nodeProps.empty()) {
            write << "extern NodeProp " << symbol << "_Cube" << cubeIdx << "_NodeProps[" << cube.nodeProps.size() << "];\n";
        }
        
        if (!cube.props.empty()) {
            write << "extern Prop " << symbol << "_Cube" << cubeIdx << "_Props[" << cube.props.size() << "];\n";
        }
    }
    
    write << "extern Cube " << symbol << "[" << levelSetup->mCubes.size() << "];\n";
    return std::nullopt;
}

ExportResult LevelSetupCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto levelSetup = std::static_pointer_cast<LevelSetupData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    // Export NodeProps for each cube
    for (size_t cubeIdx = 0; cubeIdx < levelSetup->mCubes.size(); cubeIdx++) {
        const auto& cube = levelSetup->mCubes[cubeIdx];
        
        if (!cube.nodeProps.empty()) {
            write << "NodeProp " << symbol << "_Cube" << cubeIdx << "_NodeProps[] = {\n";
            for (const auto& nodeProp : cube.nodeProps) {
                write << fourSpaceTab << "{\n";
                write << fourSpaceTab << fourSpaceTab << "/* pos */ " << nodeProp.x << ", " << nodeProp.y << ", " << nodeProp.z << ",\n";
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
                    // SpriteProp: word0(4), unk4[3](6), word8(2)
                    write << std::hex << "0x" << prop.sprite.word0 << std::dec << ", ";
                    write << "{ " << prop.sprite.unk4[0] << ", "
                          << prop.sprite.unk4[1] << ", "
                          << prop.sprite.unk4[2] << " }, ";
                    write << std::hex << "0x" << prop.sprite.word8 << std::dec;
                }
                
                write << " } },\n";
            }
            write << "};\n\n";
        }
    }

    // Export cube array
    write << "Cube " << symbol << "[] = {\n";
    for (size_t cubeIdx = 0; cubeIdx < levelSetup->mCubes.size(); cubeIdx++) {
        const auto& cube = levelSetup->mCubes[cubeIdx];
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

ExportResult LevelSetupBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto levelSetup = std::static_pointer_cast<LevelSetupData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKLevelSetup, 0);
    
    // NOTE: No cube count header - data starts directly with cube headers
    
    // Iterate through each cube
    for (const auto& cube : levelSetup->mCubes) {
        // Pack cube header (Matches your parse shifts)
        uint32_t cubeHeader = ((cube.x & 0x1F) << 27) |
                              ((cube.y & 0x1F) << 22) |
                              ((cube.z & 0x1F) << 17) |
                              ((cube.prop1Cnt & 0x3F) << 11) |
                              ((cube.prop2Cnt & 0x3F) << 5)  |
                              ((cube.unk0_4 & 0x1F) << 0);
        writer.Write(cubeHeader);

        // Write NodeProps for this cube (Matches parse loop j < cube.prop1Cnt)
        for (const auto& nodeProp : cube.nodeProps) {
            // Write raw position (Int16 x 3 = 6 bytes)
            writer.Write(nodeProp.x);
            writer.Write(nodeProp.y);
            writer.Write(nodeProp.z);

            // Pack flags1: radius:9, bit6:6, bit0:1 (2 bytes)
            uint16_t flags1 = ((nodeProp.radius & 0x1FF) << 7) |
                              ((nodeProp.bit6 & 0x3F) << 1)   |
                              ((nodeProp.bit0 & 0x1) << 0);
            writer.Write(flags1);

            // Identification/Padding (4 bytes)
            writer.Write(nodeProp.unk8);
            writer.Write(nodeProp.unkA);
            writer.Write(nodeProp.padB);

            // Pack flags2: yaw:9, scale:23 (4 bytes)
            uint32_t flags2 = ((nodeProp.yaw & 0x1FF) << 23) |
                              ((nodeProp.scale & 0x7FFFFF) << 0);
            writer.Write(flags2);

            // Pack flags3 (4 bytes)
            uint32_t flags3 = ((nodeProp.unk10_31 & 0xFFF) << 20) |
                              ((nodeProp.unk10_19 & 0xFFF) << 8)  |
                              ((nodeProp.pad10_7 & 0x1) << 7)     |
                              ((nodeProp.unk10_6 & 0x1) << 6)     |
                              ((nodeProp.pad10_5 & 0xF) << 2)     |
                              ((nodeProp.unk10_0 & 0x3) << 0);
            writer.Write(flags3);
            // Total NodeProp size: 20 bytes
        }
        
        // Write Props for this cube (properly encoded from union types)
        for (const auto& prop : cube.props) {
            const uint8_t flags = prop.raw[10];
            
            if (flags & PROP_FLAG_ACTOR) {
                // ActorProps are runtime-only, should never be in ROM
                SPDLOG_ERROR("Found ActorProp during binary export - skipping");
                continue;
            } else if (flags & PROP_FLAG_3D) {
                // ModelProp: unk0(2), yaw(1), roll(1), position[3](6), scale(1), flags(1)
                writer.Write(prop.model.unk0);
                writer.Write(prop.model.yaw);
                writer.Write(prop.model.roll);
                writer.Write(prop.model.position[0]);
                writer.Write(prop.model.position[1]);
                writer.Write(prop.model.position[2]);
                writer.Write(prop.model.scale);
                writer.Write(prop.model.flags);
            } else {
                // SpriteProp: word0(4), unk4[3](6), word8(2)
                writer.Write(prop.sprite.word0);
                writer.Write(prop.sprite.unk4[0]);
                writer.Write(prop.sprite.unk4[1]);
                writer.Write(prop.sprite.unk4[2]);
                writer.Write(prop.sprite.word8);
            }
        }
    }
    
    writer.Finish(write);
    return OffsetEntry{ 0 };
}

ExportResult LevelSetupModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto levelSetup = std::static_pointer_cast<LevelSetupData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    *replacement += ".yaml";

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "CubeCount";
    out << YAML::Value << levelSetup->mCubes.size();
    out << YAML::Key << "Cubes";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (size_t cubeIdx = 0; cubeIdx < levelSetup->mCubes.size(); cubeIdx++) {
        const auto& cube = levelSetup->mCubes[cubeIdx];
        
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
                out << YAML::Key << "X" << YAML::Value << nodeProp.x;
                out << YAML::Key << "Y" << YAML::Value << nodeProp.y;
                out << YAML::Key << "Z" << YAML::Value << nodeProp.z;
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
                    // Frame at bits [15:11] of word8 (16-bit big-endian at offset 0x0A)
                    uint8_t frame = (prop.sprite.word8 >> 11) & 0x1F;
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

std::optional<std::shared_ptr<IParsedData>> LevelSetupFactory::parse(
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

        // BK64 LevelSetup Data Layout (after decompression):
        // - No cube count header
        // - Data starts directly with packed cube headers
        // - Format: [CubeHeader1][NodeProps1...][Props1...][CubeHeader2][NodeProps2...]...
        // - Parse sequentially until end of buffer
        // 
        // Structure sizes:
        //   - Cube header: 4 bytes (packed: x:5, y:5, z:5, prop1Cnt:6, prop2Cnt:6, unk:5)
        //   - NodeProp: 20 bytes (position, radius, type, ID, yaw, scale, flags)
        //   - Prop: 12 bytes (raw union of ActorProp/ModelProp/SpriteProp)

        LUS::BinaryReader reader(decodedData.data(), decodedData.size());
        reader.SetEndianness(Torch::Endianness::Big);

        std::vector<CubeData> cubes;

        while (reader.GetBaseAddress() + 4 <= decodedData.size()) {
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
                            cube.prop1Cnt, cube.prop2Cnt, requiredBytes, remainingBytes, symbol);
                break;
            }

            // Parse NodeProps
            for (uint32_t j = 0; j < cube.prop1Cnt; j++) {
                NodeProp nodeProp;
                nodeProp.x = reader.ReadInt16();
                nodeProp.y = reader.ReadInt16();
                nodeProp.z = reader.ReadInt16();

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
                uint8_t flags = reader.ReadUByte();
                reader.Seek(currentPos, LUS::SeekOffsetType::Start);

                Prop prop;
                if (flags & PROP_FLAG_ACTOR) {
                    // ActorProps should NOT exist in ROM - skip with warning
                    SPDLOG_WARN("Found ActorProp in ROM data for {} cube {} prop {} - ActorProps are runtime-only! Skipping.", 
                                symbol, cubes.size(), j);
                    // Skip the 12 bytes
                    reader.Seek(currentPos + 12, LUS::SeekOffsetType::Start);
                    continue;  // Don't add to props vector
                } else if (flags & PROP_FLAG_3D) {
                    // ModelProp: unk0(2), yaw(1), roll(1), position[3](6), scale(1), flags(1) = 12 bytes
                    prop.model.unk0 = reader.ReadUInt16();
                    prop.model.yaw = reader.ReadUByte();
                    prop.model.roll = reader.ReadUByte();
                    prop.model.position[0] = reader.ReadInt16();
                    prop.model.position[1] = reader.ReadInt16();
                    prop.model.position[2] = reader.ReadInt16();
                    prop.model.scale = reader.ReadUByte();
                    prop.model.flags = reader.ReadUByte();
                } else {
                    // SpriteProp: word0(4), unk4[3](6), word8(2) = 12 bytes
                    prop.sprite.word0 = reader.ReadUInt32();
                    prop.sprite.unk4[0] = reader.ReadInt16();
                    prop.sprite.unk4[1] = reader.ReadInt16();
                    prop.sprite.unk4[2] = reader.ReadInt16();
                    prop.sprite.word8 = reader.ReadUInt16();
                }
                cube.props.push_back(prop);
            }

            cubes.push_back(cube);
        }

        return std::make_shared<LevelSetupData>(cubes);

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Parsing Failure for {}: {}", symbol, e.what());
        return std::nullopt;
    }
}

} // namespace BK64
