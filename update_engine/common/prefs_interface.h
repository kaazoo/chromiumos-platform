// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_PREFS_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_PREFS_INTERFACE_H_

#include <stdint.h>

#include <string>
#include <vector>

namespace chromeos_update_engine {

// The prefs interface allows access to a persistent preferences
// store. The two reasons for providing this as an interface are
// testing as well as easier switching to a new implementation in the
// future, if necessary.

class PrefsInterface {
 public:
  // Observer class to be notified about key value changes.
  class ObserverInterface {
   public:
    virtual ~ObserverInterface() = default;

    // Called when the value is set for the observed |key|.
    virtual void OnPrefSet(const std::string& key) = 0;

    // Called when the observed |key| is deleted.
    virtual void OnPrefDeleted(const std::string& key) = 0;
  };

  virtual ~PrefsInterface() = default;

  // Gets a string |value| associated with |key|. Returns true on
  // success, false on failure (including when the |key| is not
  // present in the store).
  virtual bool GetString(const std::string& key, std::string* value) const = 0;

  // Associates |key| with a string |value|. Returns true on success,
  // false otherwise.
  virtual bool SetString(const std::string& key, const std::string& value) = 0;

  // Gets an int64_t |value| associated with |key|. Returns true on
  // success, false on failure (including when the |key| is not
  // present in the store).
  virtual bool GetInt64(const std::string& key, int64_t* value) const = 0;

  // Associates |key| with an int64_t |value|. Returns true on success,
  // false otherwise.
  virtual bool SetInt64(const std::string& key, const int64_t value) = 0;

  // Gets a boolean |value| associated with |key|. Returns true on
  // success, false on failure (including when the |key| is not
  // present in the store).
  virtual bool GetBoolean(const std::string& key, bool* value) const = 0;

  // Associates |key| with a boolean |value|. Returns true on success,
  // false otherwise.
  virtual bool SetBoolean(const std::string& key, const bool value) = 0;

  // Returns true if the setting exists (i.e. a file with the given key
  // exists in the prefs directory)
  virtual bool Exists(const std::string& key) const = 0;

  // Returns true if successfully deleted the file corresponding to
  // this key. Calling with non-existent keys does nothing.
  virtual bool Delete(const std::string& key) = 0;

  // Deletes the pref key from platform and given namespace subdirectories.
  // Keys are matched against end of pref keys in each namespace.
  // Returns true if all deletes were successful.
  virtual bool Delete(const std::string& pref_key,
                      const std::vector<std::string>& nss) = 0;

  // Creates a key which is part of a sub preference.
  static std::string CreateSubKey(const std::vector<std::string>& ns_with_key);

  // Returns a list of keys within the namespace.
  virtual bool GetSubKeys(const std::string& ns,
                          std::vector<std::string>* keys) const = 0;

  // Add an observer to watch whenever the given |key| is modified. The
  // OnPrefSet() and OnPrefDelete() methods will be called whenever any of the
  // Set*() methods or the Delete() method are called on the given key,
  // respectively.
  virtual void AddObserver(const std::string& key,
                           ObserverInterface* observer) = 0;

  // Remove an observer added with AddObserver(). The observer won't be called
  // anymore for future Set*() and Delete() method calls.
  virtual void RemoveObserver(const std::string& key,
                              ObserverInterface* observer) = 0;

 protected:
  // Key separator used to create sub key and get file names,
  static const char kKeySeparator = '/';
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_PREFS_INTERFACE_H_
