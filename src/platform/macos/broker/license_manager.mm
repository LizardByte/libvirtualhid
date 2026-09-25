// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/macos/broker/license_manager.mm
 * @brief Machine-scoped Polar licensing for the macOS broker.
 */

#include "license_manager.hpp"

#include "platform/windows/shared/lvh_windows_broker_config.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#import <Foundation/Foundation.h>
#include <libvirtualhid/license.hpp>
#include <string_view>
#include <sys/stat.h>
#include <utility>

namespace lvh::detail::macos_broker {
  namespace {

    constexpr auto state_directory = "/Library/Application Support/libvirtualhid";
    constexpr auto state_path = "/Library/Application Support/libvirtualhid/license.json";
    constexpr auto evaluation_path = "/Library/Application Support/libvirtualhid/evaluation.json";
    constexpr auto validation_interval = std::chrono::hours {24};
    constexpr auto subscription_max_age = std::chrono::hours {25};
    constexpr auto outage_retention = std::chrono::hours {1};
    constexpr auto evaluation_duration = std::chrono::minutes {5};

    template<std::size_t Size>
    void set_text(std::array<char, Size> &destination, std::string_view value) {
      const auto count = std::min(value.size(), destination.size() - 1U);
      std::copy_n(value.begin(), count, destination.begin());
      destination[count] = '\0';
    }

    std::string from_ns(NSString *value) {
      return value ? std::string {[value UTF8String]} : std::string {};
    }

    NSString *to_ns(std::string_view value) {
      return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
    }

    NSString *json_string(NSDictionary *json, NSString *key) {
      id value = json[key];
      return [value isKindOfClass:[NSString class]] ? value : nil;
    }

    NSDictionary *read_protected_json(const char *path) {
      struct stat directory {};
      struct stat info {};
      if (::lstat(state_directory, &directory) != 0 || !S_ISDIR(directory.st_mode) || directory.st_uid != 0 ||
          (directory.st_mode & 0077) != 0 ||
          ::lstat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 || (info.st_mode & 0077) != 0) {
        return nil;
      }
      NSData *data = [NSData dataWithContentsOfFile:@(path)];
      if (!data) {
        return nil;
      }
      id parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
      return [parsed isKindOfClass:[NSDictionary class]] ? parsed : nil;
    }

    bool write_protected_json(const char *path, NSDictionary *json) {
      NSError *error = nil;
      if (![[NSFileManager defaultManager] createDirectoryAtPath:@(state_directory)
                                     withIntermediateDirectories:YES
                                                      attributes:@{NSFilePosixPermissions: @0700}
                                                           error:&error]) {
        return false;
      }
      struct stat directory {};
      if (::lstat(state_directory, &directory) != 0 || !S_ISDIR(directory.st_mode) || directory.st_uid != 0 ||
          ::chmod(state_directory, 0700) != 0) {
        return false;
      }
      NSData *data = [NSJSONSerialization dataWithJSONObject:json options:0 error:&error];
      if (!data || ![data writeToFile:@(path) options:NSDataWritingAtomic error:&error]) {
        return false;
      }
      return ::chmod(path, 0600) == 0;
    }

    struct ApiResult {
      bool transport_ok = false;
      NSInteger status = 0;
      bool trusted_time = false;
      NSDictionary *body = nil;
      std::string error;
    };

    ApiResult polar_request(NSString *endpoint, NSDictionary *body) {
      ApiResult result;
      @autoreleasepool {
        NSError *json_error = nil;
        NSData *payload = [NSJSONSerialization dataWithJSONObject:body options:0 error:&json_error];
        if (!payload) {
          result.error = "Unable to encode license request";
          return result;
        }
        NSURL *url = [NSURL URLWithString:[@"https://api.polar.sh" stringByAppendingString:endpoint]];
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
        request.HTTPMethod = @"POST";
        request.HTTPBody = payload;
        [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        [request setValue:@"2026-04" forHTTPHeaderField:@"Polar-Version"];
        NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = 15;
        configuration.timeoutIntervalForResource = 20;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration];
        dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        __block NSData *response_data = nil;
        __block NSHTTPURLResponse *http_response = nil;
        __block NSError *request_error = nil;
        NSURLSessionDataTask *task = [session dataTaskWithRequest:request
                                                completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                                                  response_data = data;
                                                  http_response = [response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse *) response : nil;
                                                  request_error = error;
                                                  dispatch_semaphore_signal(completed);
                                                }];
        [task resume];
        if (dispatch_semaphore_wait(completed, dispatch_time(DISPATCH_TIME_NOW, 22 * NSEC_PER_SEC)) != 0) {
          [task cancel];
          result.error = "License service timed out";
          [session invalidateAndCancel];
          return result;
        }
        [session finishTasksAndInvalidate];
        if (request_error || !http_response) {
          result.error = request_error ? from_ns(request_error.localizedDescription) : "License service did not return HTTP";
          return result;
        }
        result.transport_ok = true;
        result.status = http_response.statusCode;
        for (id key in http_response.allHeaderFields) {
          if ([key isKindOfClass:[NSString class]] && [key caseInsensitiveCompare:@"Date"] == NSOrderedSame) {
            NSString *date_header = http_response.allHeaderFields[key];
            NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
            formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
            formatter.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
            formatter.dateFormat = @"EEE, dd MMM yyyy HH:mm:ss zzz";
            result.trusted_time = [formatter dateFromString:date_header] != nil;
            break;
          }
        }
        if (response_data.length != 0) {
          id parsed = [NSJSONSerialization JSONObjectWithData:response_data options:0 error:nil];
          if ([parsed isKindOfClass:[NSDictionary class]]) {
            result.body = parsed;
          }
        }
        if (result.status >= 400) {
          NSString *detail = json_string(result.body, @"detail");
          NSString *error = json_string(result.body, @"error");
          result.error = from_ns(detail ? detail : error ? error :
                                                           @"License service rejected the request");
        }
      }
      return result;
    }

    bool allowed_benefit(std::string_view benefit_id, bool &yearly) {
      for (const auto &benefit : windows::broker_config::allowed_benefits) {
        if (benefit.id == benefit_id) {
          yearly = benefit.subscription_backed;
          return true;
        }
      }
      return false;
    }

    std::string plan_name(std::string_view benefit_id) {
      for (const auto &benefit : windows::broker_config::allowed_benefits) {
        if (benefit.id == benefit_id) {
          return std::string {benefit.plan_name};
        }
      }
      return "";
    }

    bool valid_c_string(const std::array<char, max_text_size> &value) {
      return std::memchr(value.data(), '\0', value.size()) != nullptr;
    }

  }  // namespace

  LicenseManager::LicenseManager() {
    @autoreleasepool {
      github_actions_ = std::getenv("GITHUB_ACTIONS") != nullptr &&
                        std::string_view {std::getenv("GITHUB_ACTIONS")} == "true";
      if (NSDictionary *saved = read_protected_json(state_path)) {
        State state;
        state.key = from_ns(json_string(saved, @"key"));
        state.activation_id = from_ns(json_string(saved, @"activation_id"));
        state.status = from_ns(json_string(saved, @"status"));
        state.organization_id = from_ns(json_string(saved, @"organization_id"));
        state.benefit_id = from_ns(json_string(saved, @"benefit_id"));
        state.customer_email = from_ns(json_string(saved, @"customer_email"));
        state.activation_limit = [saved[@"activation_limit"] unsignedIntValue];
        bool yearly = false;
        if (!state.key.empty() && !state.activation_id.empty() && state.status == "granted" &&
            state.organization_id == windows::broker_config::polar_organization_id &&
            allowed_benefit(state.benefit_id, yearly)) {
          state_ = std::move(state);
        }
      }
      if (github_actions_) {
        if (NSDictionary *saved = read_protected_json(evaluation_path)) {
          const auto start = [saved[@"started_at"] doubleValue];
          if (start > 0) {
            evaluation_started_at_ = std::chrono::system_clock::time_point {
              std::chrono::milliseconds {static_cast<std::int64_t>(start * 1000)}
            };
          }
        }
      }
    }
    if (state_) {
      static_cast<void>(validate());
    }
    validator_ = std::jthread {[this](std::stop_token stop) {
      background_validation(stop);
    }};
  }

  LicenseManager::~LicenseManager() {
    validator_.request_stop();
  }

  bool LicenseManager::yearly_locked() const {
    bool yearly = false;
    return state_ && allowed_benefit(state_->benefit_id, yearly) && yearly;
  }

  bool LicenseManager::licensed_locked() const {
    if (!state_ || state_->status != "granted" ||
        state_->organization_id != windows::broker_config::polar_organization_id) {
      return false;
    }
    bool yearly = false;
    if (!allowed_benefit(state_->benefit_id, yearly)) {
      return false;
    }
    return !yearly || (validated_at_ && std::chrono::steady_clock::now() - *validated_at_ < subscription_max_age);
  }

  void LicenseManager::fill_status_locked(Message &response) const {
    response.active_devices = active_devices_;
    if (!state_) {
      response.license_state = static_cast<std::uint32_t>(LicenseState::unlicensed);
      set_text(response.plan_name, github_actions_ ? "GitHub Actions Evaluation" : "Unlicensed");
      return;
    }
    response.license_state = static_cast<std::uint32_t>(licensed_locked() ? LicenseState::licensed : LicenseState::invalid);
    response.activation_limit = state_->activation_limit;
    response.activation_usage = 1;
    set_text(response.plan_name, plan_name(state_->benefit_id));
    set_text(response.customer_email, state_->customer_email);
  }

  Message LicenseManager::status() {
    Message response;
    response.type = MessageType::response;
    std::lock_guard lock {mutex_};
    fill_status_locked(response);
    set_text(response.message, licensed_locked() ? "Licensed." : state_ ? "License requires online validation." :
                                                                          "An active license is required to create virtual HID devices.");
    return response;
  }

  Message LicenseManager::activate(const Message &request) {
    if (!valid_c_string(request.license_key) || !valid_c_string(request.instance_name) || request.license_key[0] == '\0') {
      auto response = status();
      response.status = static_cast<int>(ErrorCode::invalid_argument);
      set_text(response.message, "Invalid license key or instance name");
      return response;
    }
    std::lock_guard operation_lock {operation_mutex_};
    @autoreleasepool {
      NSString *name = request.instance_name[0] ? @(request.instance_name.data()) : [[NSHost currentHost] localizedName];
      auto result = polar_request(@"/v1/customer-portal/license-keys/activate", @{@"key": @(request.license_key.data()),
                                                                                  @"organization_id": to_ns(windows::broker_config::polar_organization_id),
                                                                                  @"label": name ? name : @"Mac"});
      auto response = status();
      if (!result.transport_ok || result.status != 200) {
        const auto code = !result.transport_ok                           ? ErrorCode::network_unavailable :
                          result.status == 403                           ? ErrorCode::activation_limit_reached :
                          (result.status == 404 || result.status == 422) ? ErrorCode::license_invalid :
                                                                           ErrorCode::backend_failure;
        response.status = static_cast<int>(code);
        set_text(response.message, result.error.empty() ? "License activation failed" : result.error);
        return response;
      }
      if (!result.body || !result.trusted_time) {
        response.status = static_cast<int>(ErrorCode::backend_failure);
        set_text(response.message, "License activation response is missing state or trusted server time");
        return response;
      }
      NSString *activation_id = json_string(result.body, @"id");
      NSDictionary *license = result.body[@"license_key"];
      if (![license isKindOfClass:[NSDictionary class]]) {
        response.status = static_cast<int>(ErrorCode::backend_failure);
        set_text(response.message, "License activation response is missing license state");
        return response;
      }
      State state;
      state.key = request.license_key.data();
      state.activation_id = from_ns(activation_id);
      state.status = from_ns(json_string(license, @"status"));
      state.organization_id = from_ns(json_string(license, @"organization_id"));
      state.benefit_id = from_ns(json_string(license, @"benefit_id"));
      state.activation_limit = [license[@"limit_activations"] unsignedIntValue];
      NSDictionary *customer = license[@"customer"];
      if ([customer isKindOfClass:[NSDictionary class]]) {
        state.customer_email = from_ns(json_string(customer, @"email"));
      }
      bool yearly = false;
      if (state.activation_id.empty() || state.status != "granted" ||
          state.organization_id != windows::broker_config::polar_organization_id ||
          !allowed_benefit(state.benefit_id, yearly)) {
        response.status = static_cast<int>(ErrorCode::license_invalid);
        set_text(response.message, "License organization, benefit, or activation is not allowed");
        return response;
      }
      if (!write_protected_json(state_path, @{@"key": to_ns(state.key),
                                              @"activation_id": to_ns(state.activation_id),
                                              @"status": to_ns(state.status),
                                              @"organization_id": to_ns(state.organization_id),
                                              @"benefit_id": to_ns(state.benefit_id),
                                              @"customer_email": to_ns(state.customer_email),
                                              @"activation_limit": @(state.activation_limit)})) {
        response.status = static_cast<int>(ErrorCode::backend_failure);
        set_text(response.message, "Unable to securely save machine license");
        return response;
      }
      {
        std::lock_guard lock {mutex_};
        state_ = std::move(state);
        validated_at_ = std::chrono::steady_clock::now();
        unavailable_since_.reset();
        online_confirmed_ = true;
        fill_status_locked(response);
      }
      set_text(response.message, "License activated on this machine.");
      return response;
    }
  }

  Message LicenseManager::validate() {
    std::lock_guard operation_lock {operation_mutex_};
    State state;
    {
      std::lock_guard lock {mutex_};
      if (!state_) {
        auto response = Message {};
        response.type = MessageType::response;
        response.status = static_cast<int>(ErrorCode::license_required);
        fill_status_locked(response);
        set_text(response.message, "No license is activated on this machine.");
        return response;
      }
      state = *state_;
    }
    @autoreleasepool {
      auto result = polar_request(@"/v1/customer-portal/license-keys/validate", @ {
        @"key": to_ns(state.key),
        @"organization_id": to_ns(windows::broker_config::polar_organization_id),
        @"activation_id": to_ns(state.activation_id)
      });
      if (!result.transport_ok || result.status != 200 || !result.body || !result.trusted_time) {
        {
          std::lock_guard lock {mutex_};
          online_confirmed_ = false;
          if (!unavailable_since_) {
            unavailable_since_ = std::chrono::steady_clock::now();
          }
          if (result.status == 404) {
            state_.reset();
            validated_at_.reset();
          }
        }
        if (result.status == 404) {
          [[NSFileManager defaultManager] removeItemAtPath:@(state_path) error:nil];
        }
        auto response = status();
        response.status = static_cast<int>(result.status == 404 ? ErrorCode::license_invalid : !result.transport_ok ? ErrorCode::network_unavailable :
                                                                                                                      ErrorCode::backend_failure);
        set_text(response.message, result.error.empty() ? "License validation failed" : result.error);
        return response;
      }
      NSDictionary *activation = result.body[@"activation"];
      NSString *activation_id = [activation isKindOfClass:[NSDictionary class]] ? json_string(activation, @"id") : nil;
      const auto new_status = from_ns(json_string(result.body, @"status"));
      const auto new_organization = from_ns(json_string(result.body, @"organization_id"));
      const auto new_benefit = from_ns(json_string(result.body, @"benefit_id"));
      bool yearly = false;
      if (from_ns(activation_id) != state.activation_id || new_status != "granted" ||
          new_organization != windows::broker_config::polar_organization_id ||
          !allowed_benefit(new_benefit, yearly)) {
        {
          std::lock_guard lock {mutex_};
          state_.reset();
          validated_at_.reset();
        }
        [[NSFileManager defaultManager] removeItemAtPath:@(state_path) error:nil];
        auto response = status();
        response.status = static_cast<int>(ErrorCode::license_invalid);
        set_text(response.message, "License is revoked, disabled, or has an invalid benefit");
        return response;
      }
      state.status = new_status;
      state.benefit_id = new_benefit;
      state.activation_limit = [result.body[@"limit_activations"] unsignedIntValue];
      NSDictionary *customer = result.body[@"customer"];
      if ([customer isKindOfClass:[NSDictionary class]]) {
        state.customer_email = from_ns(json_string(customer, @"email"));
      }
      if (!write_protected_json(state_path, @{@"key": to_ns(state.key),
                                              @"activation_id": to_ns(state.activation_id),
                                              @"status": to_ns(state.status),
                                              @"organization_id": to_ns(state.organization_id),
                                              @"benefit_id": to_ns(state.benefit_id),
                                              @"customer_email": to_ns(state.customer_email),
                                              @"activation_limit": @(state.activation_limit)})) {
        auto response = status();
        response.status = static_cast<int>(ErrorCode::backend_failure);
        set_text(response.message, "Unable to securely save validated license");
        return response;
      }
      {
        std::lock_guard lock {mutex_};
        state_ = std::move(state);
        validated_at_ = std::chrono::steady_clock::now();
        unavailable_since_.reset();
        online_confirmed_ = true;
      }
      auto response = status();
      set_text(response.message, "License validated.");
      return response;
    }
  }

  Message LicenseManager::deactivate() {
    std::lock_guard operation_lock {operation_mutex_};
    State state;
    {
      std::lock_guard lock {mutex_};
      if (!state_) {
        auto response = Message {};
        response.type = MessageType::response;
        fill_status_locked(response);
        set_text(response.message, "No machine license is active.");
        return response;
      }
      state = *state_;
    }
    @autoreleasepool {
      auto result = polar_request(@"/v1/customer-portal/license-keys/deactivate", @ {
        @"key": to_ns(state.key),
        @"organization_id": to_ns(windows::broker_config::polar_organization_id),
        @"activation_id": to_ns(state.activation_id)
      });
      if (!result.transport_ok || (result.status != 204 && result.status != 404)) {
        auto response = status();
        response.status = static_cast<int>(!result.transport_ok ? ErrorCode::network_unavailable : ErrorCode::backend_failure);
        set_text(response.message, result.error.empty() ? "License deactivation failed" : result.error);
        return response;
      }
      [[NSFileManager defaultManager] removeItemAtPath:@(state_path) error:nil];
      {
        std::lock_guard lock {mutex_};
        state_.reset();
        validated_at_.reset();
        online_confirmed_ = false;
      }
      auto response = status();
      set_text(response.message, "License deactivated on this machine.");
      return response;
    }
  }

  Message LicenseManager::handle(const Message &request) {
    switch (request.type) {
      case MessageType::status:
        return status();
      case MessageType::activate:
        return activate(request);
      case MessageType::validate:
        return validate();
      case MessageType::deactivate:
        return deactivate();
      default:
        {
          auto response = status();
          response.status = static_cast<int>(ErrorCode::invalid_argument);
          set_text(response.message, "Invalid license request");
          return response;
        }
    }
  }

  bool LicenseManager::authorize_create(Message &response, bool &evaluation) {
    response.type = MessageType::response;
    std::lock_guard lock {mutex_};
    fill_status_locked(response);
    if (licensed_locked()) {
      if (online_confirmed_ || active_licensed_devices_ == 0) {
        evaluation = false;
        return true;
      }
      response.status = static_cast<int>(ErrorCode::network_unavailable);
      set_text(response.message, "Polar is unavailable; the one-device fallback is already in use");
      return false;
    }
    if (!state_ && github_actions_) {
      const auto now = std::chrono::system_clock::now();
      if (!evaluation_started_at_) {
        const auto seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
        bool saved = false;
        @autoreleasepool {
          saved = write_protected_json(evaluation_path, @{@"started_at": @(seconds)});
        }
        if (!saved) {
          response.status = static_cast<int>(ErrorCode::backend_failure);
          set_text(response.message, "Unable to save GitHub Actions evaluation state");
          return false;
        }
        evaluation_started_at_ = now;
      }
      if (now >= *evaluation_started_at_ && now < *evaluation_started_at_ + evaluation_duration) {
        evaluation = true;
        return true;
      }
    }
    response.status = static_cast<int>(state_ ? ErrorCode::license_invalid : ErrorCode::license_required);
    set_text(response.message, state_ ? "License requires online validation" : "An active license is required");
    return false;
  }

  bool LicenseManager::device_is_authorized(bool evaluation) {
    std::lock_guard lock {mutex_};
    if (evaluation) {
      const auto now = std::chrono::system_clock::now();
      return evaluation_started_at_ && now >= *evaluation_started_at_ && now < *evaluation_started_at_ + evaluation_duration;
    }
    if (!licensed_locked()) {
      return false;
    }
    return !unavailable_since_ || std::chrono::steady_clock::now() - *unavailable_since_ < outage_retention;
  }

  void LicenseManager::add_device(bool evaluation) {
    std::lock_guard lock {mutex_};
    ++active_devices_;
    if (!evaluation) {
      ++active_licensed_devices_;
    }
  }

  void LicenseManager::remove_device(bool evaluation) {
    std::lock_guard lock {mutex_};
    --active_devices_;
    if (!evaluation) {
      --active_licensed_devices_;
    }
  }

  void LicenseManager::background_validation(std::stop_token stop) {
    while (!stop.stop_requested()) {
      for (int minute = 0; minute < 60 && !stop.stop_requested(); ++minute) {
        std::this_thread::sleep_for(std::chrono::seconds {1});
      }
      if (stop.stop_requested()) {
        break;
      }
      bool due = false;
      {
        std::lock_guard lock {mutex_};
        due = state_ && (!validated_at_ || std::chrono::steady_clock::now() - *validated_at_ >= validation_interval);
      }
      if (due) {
        static_cast<void>(validate());
      }
    }
  }

}  // namespace lvh::detail::macos_broker
