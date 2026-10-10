/**
 * @file tests/unit/test_virtualhid_control_license_panel.cpp
 * @brief Headless UI tests for Windows and macOS machine license management.
 */

// standard includes
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// third-party includes
#include <gtest/gtest.h>
#include <imgui_internal.h>
// GoogleTest owns the entry point, including on Windows.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#undef SDL_MAIN_HANDLED

// local includes
#include <virtualhid_control_model.hpp>

namespace {
  struct LicensePanelClient {
    lvh::LicenseResult result;
    int status_calls = 0;
    int activation_calls = 0;
    int validation_calls = 0;
    int deactivation_calls = 0;
    std::string activated_key;
  };

  /**
   * @brief Access the fake license client attached to the current test's ImGui context.
   *
   * @return Reference to the client owned by the active test fixture.
   */
  LicensePanelClient &license_panel_client() {
    return *static_cast<LicensePanelClient *>(ImGui::GetIO().UserData);
  }
}  // namespace

namespace lvh {
  LicenseResult control_test_get_license_status() {
    auto &client = license_panel_client();
    ++client.status_calls;
    return client.result;
  }

  LicenseResult control_test_activate_license(std::string_view key) {
    auto &client = license_panel_client();
    ++client.activation_calls;
    client.activated_key = key;
    return client.result;
  }

  LicenseResult control_test_validate_license() {
    auto &client = license_panel_client();
    ++client.validation_calls;
    return client.result;
  }

  LicenseResult control_test_deactivate_license() {
    auto &client = license_panel_client();
    ++client.deactivation_calls;
    return client.result;
  }
}  // namespace lvh

// Load the UI with a fake license client so button tests never change a real
// machine activation. Rename its entry point after loading SDL's main header.
#ifdef main
  #undef main
#endif
#define main virtualhid_control_test_main
#define get_license_status control_test_get_license_status
#define activate_license control_test_activate_license
#define validate_license control_test_validate_license
#define deactivate_license control_test_deactivate_license
#include "../../tools/virtualhid_control.cpp"
#undef deactivate_license
#undef validate_license
#undef activate_license
#undef get_license_status
#undef main

namespace {
  using lvh::tools::virtualhid_control::ui::LicensePanel;

  class VirtualHidControlLicensePanelTest: public testing::Test {
  protected:
    void SetUp() override {
      client_.result.status = lvh::OperationStatus::success();
      client_.result.license.service_available = true;
      client_.result.license.state = lvh::LicenseState::unlicensed;
      ImGui::CreateContext();
      auto &io = ImGui::GetIO();
      io.UserData = &client_;
      io.IniFilename = nullptr;
      io.DisplaySize = {800.0F, 1000.0F};
      io.DeltaTime = 1.0F / 60.0F;
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      unsigned char *pixels = nullptr;
      int width = 0;
      int height = 0;
      io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
      panel_ = std::make_unique<LicensePanel>();
      render();
    }

    void TearDown() override {
      panel_.reset();
      ImGui::DestroyContext();
    }

    void render() {
      ImGui::NewFrame();
      ImGui::SetNextWindowPos({0.0F, 0.0F});
      ImGui::SetNextWindowSize({800.0F, 1000.0F});
      ImGui::Begin("License panel test");
      panel_->render([this](std::string_view error) {
        errors_.emplace_back(error);
      });
      ImGui::End();
      ImGui::Render();
    }

    void activate_item(const char *label) {
      auto *window = ImGui::FindWindowByName("License panel test");
      ASSERT_NE(window, nullptr);
      ImGui::ActivateItemByID(window->GetID(label));
      render();
    }

    void enter_key(const char *key) {
      activate_item("##license-key");
      ImGui::GetIO().AddInputCharactersUTF8(key);
      render();
    }

    std::string key_text() {
      activate_item("##license-key");
      return ImGui::GetCurrentContext()->InputTextState.GetText();
    }

    LicensePanelClient &client() {
      return client_;
    }

    const std::vector<std::string> &errors() const {
      return errors_;
    }

    void refresh_license() {
      panel_->refresh();
      render();
    }

  private:
    LicensePanelClient client_;
    std::unique_ptr<LicensePanel> panel_;
    std::vector<std::string> errors_;
  };
}  // namespace

TEST_F(VirtualHidControlLicensePanelTest, ActivatesTrimmedKeyAndClearsSuccessfulInput) {
  enter_key("  test-license-key  ");
  activate_item("Activate license");

  EXPECT_EQ(client().activation_calls, 1);
  EXPECT_EQ(client().activated_key, "test-license-key");
  EXPECT_TRUE(errors().empty());
  EXPECT_TRUE(key_text().empty());
}

TEST_F(VirtualHidControlLicensePanelTest, DisablesActivationForEmptyAndWhitespaceKeys) {
  activate_item("Activate license");
  EXPECT_EQ(client().activation_calls, 0);
  enter_key("   ");
  activate_item("Activate license");
  EXPECT_EQ(client().activation_calls, 0);
  EXPECT_TRUE(errors().empty());
}

TEST_F(VirtualHidControlLicensePanelTest, ReportsFailedActivationAndRetainsKeyForRetry) {
  client().result.status = lvh::OperationStatus::failure(lvh::ErrorCode::network_unavailable, "License service unavailable.");
  enter_key("test-license-key");
  activate_item("Activate license");

  EXPECT_EQ(client().activation_calls, 1);
  EXPECT_EQ(errors(), (std::vector<std::string> {"License service unavailable."}));
  EXPECT_EQ(key_text(), "test-license-key");

  client().result.status = lvh::OperationStatus::success();
  activate_item("Activate license");
  EXPECT_EQ(client().activation_calls, 2);
  EXPECT_EQ(client().activated_key, "test-license-key");
  EXPECT_TRUE(key_text().empty());
}

TEST_F(VirtualHidControlLicensePanelTest, RefreshValidatesOnlineAndReportsFailures) {
  activate_item("Refresh");
  EXPECT_EQ(client().validation_calls, 1);
  EXPECT_EQ(client().status_calls, 1);
  EXPECT_TRUE(errors().empty());

  client().result.status = lvh::OperationStatus::failure(lvh::ErrorCode::network_unavailable, "Validation unavailable.");
  activate_item("Refresh");
  EXPECT_EQ(client().validation_calls, 2);
  EXPECT_EQ(errors(), (std::vector<std::string> {"Validation unavailable."}));
}

TEST_F(VirtualHidControlLicensePanelTest, DeactivatesMachineAndReportsFailures) {
  activate_item("Deactivate this machine");
  EXPECT_EQ(client().deactivation_calls, 1);
  EXPECT_TRUE(errors().empty());

  client().result.status = lvh::OperationStatus::failure(lvh::ErrorCode::network_unavailable, "Deactivation unavailable.");
  activate_item("Deactivate this machine");
  EXPECT_EQ(client().deactivation_calls, 2);
  EXPECT_EQ(errors(), (std::vector<std::string> {"Deactivation unavailable."}));
}

TEST_F(VirtualHidControlLicensePanelTest, DisablesValidationAndDeactivationWithoutBroker) {
  client().result.license.service_available = false;
  refresh_license();

  activate_item("Refresh");
  activate_item("Deactivate this machine");
  EXPECT_EQ(client().validation_calls, 0);
  EXPECT_EQ(client().deactivation_calls, 0);
  EXPECT_TRUE(errors().empty());
}
