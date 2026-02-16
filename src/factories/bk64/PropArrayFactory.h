#pragma once

#include <factories/BaseFactory.h>
#include <vector>

namespace BK64 {

// Forward declare Prop from LevelSetupFactory.h
union Prop;

class PropArrayData : public IParsedData {
  public:
    std::vector<Prop> mProps;
    std::string mParentSymbol;
    size_t mCubeIndex;

    PropArrayData(std::vector<Prop> props, std::string parentSymbol, size_t cubeIndex)
        : mProps(std::move(props)), mParentSymbol(parentSymbol), mCubeIndex(cubeIndex) {}
};

class PropArrayHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PropArrayBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PropArrayCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PropArrayFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, PropArrayCodeExporter)
            REGISTER(Header, PropArrayHeaderExporter)
            REGISTER(Binary, PropArrayBinaryExporter)
        };
    }
};

} // namespace BK64
