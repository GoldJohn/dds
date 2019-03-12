/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/client/read_preference.h"

#include <string>

#include "mongo/base/status.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/bsontypes.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {
namespace {

const char kModeFieldName[] = "mode";
const char kTagsFieldName[] = "tags";
const char kMaxStalenessSecondsFieldName[] = "maxStalenessSeconds";
const char kcustomerMetaName[] = "customerMeta";

const char kPrimaryOnly[] = "primary";
const char kPrimaryPreferred[] = "primaryPreferred";
const char kSecondaryOnly[] = "secondary";
const char kSecondaryPreferred[] = "secondaryPreferred";
const char kNearest[] = "nearest";

// Avoid overflow errors when converting from seconds to milliseconds
const auto kMaximalMaxStalenessSecondsValue(durationCount<Seconds>(Milliseconds::max()));

StringData readPreferenceName(ReadPreference pref) {
    switch (pref) {
        case ReadPreference::PrimaryOnly:
            return StringData(kPrimaryOnly);
        case ReadPreference::PrimaryPreferred:
            return StringData(kPrimaryPreferred);
        case ReadPreference::SecondaryOnly:
            return StringData(kSecondaryOnly);
        case ReadPreference::SecondaryPreferred:
            return StringData(kSecondaryPreferred);
        case ReadPreference::Nearest:
            return StringData(kNearest);
        default:
            MONGO_UNREACHABLE;
    }
}

StatusWith<ReadPreference> parseReadPreferenceMode(StringData prefStr) {
    if (prefStr == kPrimaryOnly) {
        return ReadPreference::PrimaryOnly;
    } else if (prefStr == kPrimaryPreferred) {
        return ReadPreference::PrimaryPreferred;
    } else if (prefStr == kSecondaryOnly) {
        return ReadPreference::SecondaryOnly;
    } else if (prefStr == kSecondaryPreferred) {
        return ReadPreference::SecondaryPreferred;
    } else if (prefStr == kNearest) {
        return ReadPreference::Nearest;
    }
    return Status(ErrorCodes::FailedToParse,
                  str::stream() << "Could not parse $readPreference mode '" << prefStr
                                << "'. Only the modes '"
                                << kPrimaryOnly
                                << "', '"
                                << kPrimaryPreferred
                                << "', "
                                << kSecondaryOnly
                                << "', '"
                                << kSecondaryPreferred
                                << "', and '"
                                << kNearest
                                << "' are supported.");
}

// Slight kludge here: if we weren't passed a TagSet, we default to the empty
// TagSet if ReadPreference is Primary, or the default (wildcard) TagSet otherwise.
// This maintains compatibility with existing code, while preserving the ability to round
// trip.
TagSet defaultTagSetForMode(ReadPreference mode) {
    switch (mode) {
        case ReadPreference::PrimaryOnly:
            return TagSet::primaryOnly();
        default:
            return TagSet();
    }
}

}  // namespace


/**
 * Replica set refresh period on the task executor.
 */
const Milliseconds ReadPreferenceSetting::kMinimalMaxStalenessValue(60000);

TagSet::TagSet() : _tags(BSON_ARRAY(BSONObj())) {}

TagSet TagSet::primaryOnly() {
    return TagSet{BSONArray()};
}

ReadPreferenceSetting::ReadPreferenceSetting(ReadPreference pref,
                                             TagSet tags,
                                             Milliseconds maxStalenessMS)
    : pref(std::move(pref)),
      tags(std::move(tags)),
      maxStalenessMS(std::move(maxStalenessMS)),
      customerMeta(false) {}

ReadPreferenceSetting::ReadPreferenceSetting(ReadPreference pref,
                                             TagSet tags,
                                             Milliseconds maxStalenessMS,
                                             bool customerMeta)
    : pref(std::move(pref)),
      tags(std::move(tags)),
      maxStalenessMS(std::move(maxStalenessMS)),
      customerMeta(customerMeta) {}

ReadPreferenceSetting::ReadPreferenceSetting(ReadPreference pref, Milliseconds maxStalenessMS)
    : ReadPreferenceSetting(pref, defaultTagSetForMode(pref), maxStalenessMS) {}

ReadPreferenceSetting::ReadPreferenceSetting(ReadPreference pref, TagSet tags)
    : pref(std::move(pref)), tags(std::move(tags)), customerMeta(false) {}

ReadPreferenceSetting::ReadPreferenceSetting(ReadPreference pref, TagSet tags, bool customerMeta)
    : pref(std::move(pref)), tags(std::move(tags)), customerMeta(customerMeta) {}

ReadPreferenceSetting::ReadPreferenceSetting(ReadPreference pref)
    : ReadPreferenceSetting(pref, defaultTagSetForMode(pref)) {}

StatusWith<ReadPreferenceSetting> ReadPreferenceSetting::fromBSON(const BSONObj& readPrefObj) {
    std::string modeStr;
    auto modeExtractStatus = bsonExtractStringField(readPrefObj, kModeFieldName, &modeStr);
    if (!modeExtractStatus.isOK()) {
        return modeExtractStatus;
    }

    ReadPreference mode;
    auto swReadPrefMode = parseReadPreferenceMode(modeStr);
    if (!swReadPrefMode.isOK()) {
        return swReadPrefMode.getStatus();
    }
    mode = std::move(swReadPrefMode.getValue());

    TagSet tags;
    BSONElement tagsElem;
    auto tagExtractStatus =
        bsonExtractTypedField(readPrefObj, kTagsFieldName, mongo::Array, &tagsElem);
    if (tagExtractStatus.isOK()) {
        tags = TagSet{BSONArray(tagsElem.Obj().getOwned())};

        // In accordance with the read preference spec, passing the default wildcard tagset
        // '[{}]' is the same as not passing a TagSet at all. Furthermore, passing an empty
        // TagSet with a non-primary ReadPreference is equivalent to passing the wildcard
        // ReadPreference.
        if (tags == TagSet() || tags == TagSet::primaryOnly()) {
            tags = defaultTagSetForMode(mode);
        }

        // If we are using a user supplied TagSet, check that it is compatible with
        // the readPreference mode.
        else if (ReadPreference::PrimaryOnly == mode && (tags != TagSet::primaryOnly())) {
            return Status(ErrorCodes::BadValue,
                          "Only empty tags are allowed with primary read preference");
        }
    }

    else if (ErrorCodes::NoSuchKey == tagExtractStatus) {
        tags = defaultTagSetForMode(mode);
    } else {
        return tagExtractStatus;
    }

    double maxStalenessSecondsValue;

    Status maxStalenessSecondsExtractStatus = bsonExtractDoubleField(
        readPrefObj, kMaxStalenessSecondsFieldName, &maxStalenessSecondsValue);

    if (maxStalenessSecondsExtractStatus == ErrorCodes::NoSuchKey) {
        return ReadPreferenceSetting(mode, tags);
    } else if (!maxStalenessSecondsExtractStatus.isOK()) {
        return maxStalenessSecondsExtractStatus;
    }

    if (maxStalenessSecondsValue < 0.0) {
        return {ErrorCodes::BadValue,
                str::stream() << kMaxStalenessSecondsFieldName << " value can not be negative"};
    }

    if (maxStalenessSecondsValue > kMaximalMaxStalenessSecondsValue) {
        return {ErrorCodes::MaxStalenessOutOfRange,
                str::stream() << kMaxStalenessSecondsFieldName << " value can not exceed "
                              << kMaximalMaxStalenessSecondsValue};
    }

    if (maxStalenessSecondsValue == 0.0) {
        return ReadPreferenceSetting(mode, tags);
    }

    // Use a lambda to do the double seconds to integer milliseconds conversion in order to
    // encapsulate the usage of helper variables
    const Milliseconds requestedMaxStalenessMS = [maxStalenessSecondsValue] {
        double integerPart;
        const double fractionalPart = std::modf(maxStalenessSecondsValue, &integerPart);

        return Seconds(static_cast<long long>(integerPart)) +
            Milliseconds(static_cast<long long>(fractionalPart *
                                                durationCount<Milliseconds>(Seconds(1))));
    }();

    if (requestedMaxStalenessMS < kMinimalMaxStalenessValue) {
        return {ErrorCodes::MaxStalenessOutOfRange,
                str::stream() << kMaxStalenessSecondsFieldName << " value can not be less than "
                              << kMinimalMaxStalenessValue};
    }

    if (mode == ReadPreference::PrimaryOnly) {
        return {ErrorCodes::BadValue,
                str::stream() << kMaxStalenessSecondsFieldName
                              << " can not be set for the primary mode"};
    }

    bool customerMeta = false;
    bsonExtractBooleanFieldWithDefault(readPrefObj, kcustomerMetaName, false, &customerMeta);

    StatusWith<ReadPreferenceSetting> ret =
        ReadPreferenceSetting(mode, tags, requestedMaxStalenessMS);

    if (ret.isOK()) {
        ret.getValue().customerMeta = customerMeta;
    }

    return ret;
}

BSONObj ReadPreferenceSetting::toBSON() const {
    BSONObjBuilder bob;
    bob.append(kModeFieldName, readPreferenceName(pref));
    if (tags != defaultTagSetForMode(pref)) {
        bob.append(kTagsFieldName, tags.getTagBSON());
    }
    if (maxStalenessMS.count() > 0) {
        bob.append(kMaxStalenessSecondsFieldName,
                   static_cast<double>(maxStalenessMS.count()) /
                       durationCount<Milliseconds>(Seconds(1)));
    }
    if (customerMeta != false) {
        bob.append(kcustomerMetaName, customerMeta);
    }
    return bob.obj();
}

std::string ReadPreferenceSetting::toString() const {
    return toBSON().toString();
}

}  // namespace mongo
