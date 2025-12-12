//
// Created by gregorian on 10/12/2025.
//

#include <gtest/gtest.h>
#include "bha/export/exporter.h"
#include "bha/export/json_exporter.h"

using namespace bha::export_module;

class ExporterFactoryTest : public ::testing::Test {
};

TEST_F(ExporterFactoryTest, CreateJSONExporter) {
    const auto exporter = ExporterFactory::create_exporter(ExportFormat::JSON);

    ASSERT_NE(exporter, nullptr);
    EXPECT_EQ(exporter->get_format(), ExportFormat::JSON);
    EXPECT_EQ(exporter->get_default_extension(), ".json");
}

TEST_F(ExporterFactoryTest, CreateHTMLExporter) {
    const auto exporter = ExporterFactory::create_exporter(ExportFormat::HTML);

    ASSERT_NE(exporter, nullptr);
    EXPECT_EQ(exporter->get_format(), ExportFormat::HTML);
    EXPECT_EQ(exporter->get_default_extension(), ".html");
}

TEST_F(ExporterFactoryTest, CreateCSVExporter) {
    const auto exporter = ExporterFactory::create_exporter(ExportFormat::CSV);

    ASSERT_NE(exporter, nullptr);
    EXPECT_EQ(exporter->get_format(), ExportFormat::CSV);
    EXPECT_EQ(exporter->get_default_extension(), ".csv");
}

TEST_F(ExporterFactoryTest, CreateMarkdownExporter) {
    const auto exporter = ExporterFactory::create_exporter(ExportFormat::MARKDOWN);

    ASSERT_NE(exporter, nullptr);
    EXPECT_EQ(exporter->get_format(), ExportFormat::MARKDOWN);
    EXPECT_EQ(exporter->get_default_extension(), ".md");
}

TEST_F(ExporterFactoryTest, CreateTextExporter) {
    const auto exporter = ExporterFactory::create_exporter(ExportFormat::TEXT);

    ASSERT_NE(exporter, nullptr);
    EXPECT_EQ(exporter->get_format(), ExportFormat::TEXT);
    EXPECT_EQ(exporter->get_default_extension(), ".txt");
}

TEST_F(ExporterFactoryTest, CreateEachType) {
    const std::vector formats = {
        ExportFormat::JSON,
        ExportFormat::HTML,
        ExportFormat::CSV,
        ExportFormat::MARKDOWN,
        ExportFormat::TEXT
    };

    for (const auto& format : formats) {
        auto exporter = ExporterFactory::create_exporter(format);
        ASSERT_NE(exporter, nullptr);
        EXPECT_EQ(exporter->get_format(), format);
    }
}

TEST_F(ExporterFactoryTest, FormatFromString_JSON) {
    const auto format = ExporterFactory::format_from_string("json");
    EXPECT_EQ(format, ExportFormat::JSON);
}

TEST_F(ExporterFactoryTest, FormatFromString_HTML) {
    const auto format = ExporterFactory::format_from_string("html");
    EXPECT_EQ(format, ExportFormat::HTML);
}

TEST_F(ExporterFactoryTest, FormatFromString_CSV) {
    const auto format = ExporterFactory::format_from_string("csv");
    EXPECT_EQ(format, ExportFormat::CSV);
}

TEST_F(ExporterFactoryTest, FormatFromString_Markdown) {
    const auto format = ExporterFactory::format_from_string("markdown");
    EXPECT_EQ(format, ExportFormat::MARKDOWN);
}

TEST_F(ExporterFactoryTest, FormatFromString_Text) {
    const auto format = ExporterFactory::format_from_string("text");
    EXPECT_EQ(format, ExportFormat::TEXT);
}

TEST_F(ExporterFactoryTest, FormatFromString_CaseInsensitive) {
    const auto format1 = ExporterFactory::format_from_string("JSON");
    const auto format2 = ExporterFactory::format_from_string("Json");

    EXPECT_EQ(format1, ExportFormat::JSON);
    EXPECT_EQ(format2, ExportFormat::JSON);
}

TEST_F(ExporterFactoryTest, FormatToString_JSON) {
    const auto str = ExporterFactory::format_to_string(ExportFormat::JSON);
    EXPECT_EQ(str, "json");
}

TEST_F(ExporterFactoryTest, FormatToString_HTML) {
    const auto str = ExporterFactory::format_to_string(ExportFormat::HTML);
    EXPECT_EQ(str, "html");
}

TEST_F(ExporterFactoryTest, FormatToString_CSV) {
    const auto str = ExporterFactory::format_to_string(ExportFormat::CSV);
    EXPECT_EQ(str, "csv");
}

TEST_F(ExporterFactoryTest, FormatToString_Markdown) {
    const auto str = ExporterFactory::format_to_string(ExportFormat::MARKDOWN);
    EXPECT_EQ(str, "markdown");
}

TEST_F(ExporterFactoryTest, FormatToString_Text) {
    const auto str = ExporterFactory::format_to_string(ExportFormat::TEXT);
    EXPECT_EQ(str, "text");
}

TEST_F(ExporterFactoryTest, FormatConversionRoundTrip_JSON) {
    const auto format = ExporterFactory::format_from_string("json");
    const auto str = ExporterFactory::format_to_string(format);
    const auto format2 = ExporterFactory::format_from_string(str);

    EXPECT_EQ(format, format2);
    EXPECT_EQ(format, ExportFormat::JSON);
}

TEST_F(ExporterFactoryTest, FormatConversionRoundTrip_HTML) {
    const auto format = ExporterFactory::format_from_string("html");
    const auto str = ExporterFactory::format_to_string(format);
    const auto format2 = ExporterFactory::format_from_string(str);

    EXPECT_EQ(format, format2);
    EXPECT_EQ(format, ExportFormat::HTML);
}

TEST_F(ExporterFactoryTest, FormatConversionRoundTrip_CSV) {
    const auto format = ExporterFactory::format_from_string("csv");
    const auto str = ExporterFactory::format_to_string(format);
    const auto format2 = ExporterFactory::format_from_string(str);

    EXPECT_EQ(format, format2);
    EXPECT_EQ(format, ExportFormat::CSV);
}

TEST_F(ExporterFactoryTest, FormatConversionRoundTrip_Markdown) {
    const auto format = ExporterFactory::format_from_string("markdown");
    const auto str = ExporterFactory::format_to_string(format);
    const auto format2 = ExporterFactory::format_from_string(str);

    EXPECT_EQ(format, format2);
    EXPECT_EQ(format, ExportFormat::MARKDOWN);
}

TEST_F(ExporterFactoryTest, FormatConversionRoundTrip_Text) {
    const auto format = ExporterFactory::format_from_string("text");
    const auto str = ExporterFactory::format_to_string(format);
    const auto format2 = ExporterFactory::format_from_string(str);

    EXPECT_EQ(format, format2);
    EXPECT_EQ(format, ExportFormat::TEXT);
}