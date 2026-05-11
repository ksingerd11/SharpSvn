// Copyright 2007-2025 The SharpSvn Project
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#pragma once

#include "SvnEnums.h"
#include "AprPool.h"

namespace SharpSvn {
    ref class SvnClient;

    public ref class SvnConflictOption sealed
    {
        initonly SvnConflictOptionId _id;
        initonly String^ _label;
        initonly String^ _description;

    internal:
        SvnConflictOption(SvnConflictOptionId id, String^ label, String^ description)
        {
            _id = id;
            _label = label ? label : String::Empty;
            _description = description ? description : String::Empty;
        }

    public:
        property SvnConflictOptionId Id
        {
            SvnConflictOptionId get()
            {
                return _id;
            }
        }

        property String^ Label
        {
            String^ get()
            {
                return _label;
            }
        }

        property String^ Description
        {
            String^ get()
            {
                return _description;
            }
        }

        virtual String^ ToString() override
        {
            return String::IsNullOrEmpty(Label) ? Id.ToString() : Label;
        }
    };

    public ref class SvnConflictInfo sealed : public EventArgs
    {
        SvnClient^ _owner;
        SharpSvn::Implementation::AprPool^ _pool;
        svn_client_conflict_t *_conflict;

        initonly String^ _fullPath;
        initonly bool _hasTreeConflict;
        initonly bool _hasTextConflict;
        initonly int _propConflictCount;
        initonly SvnOperation _operation;
        initonly SvnConflictAction _incomingChange;
        initonly SvnConflictReason _localChange;
        initonly SvnNodeKind _victimNodeKind;
        initonly SvnConflictOptionId _recommendedOptionId;

        String^ _incomingChangeSummary;
        String^ _localChangeSummary;
        Uri^ _repositoryRoot;
        String^ _repositoryUuid;
        String^ _incomingOldRepositoryPath;
        __int64 _incomingOldRevision;
        SvnNodeKind _incomingOldNodeKind;
        String^ _incomingNewRepositoryPath;
        __int64 _incomingNewRevision;
        SvnNodeKind _incomingNewNodeKind;

    internal:
        SvnConflictInfo(
            SvnClient^ owner,
            svn_client_conflict_t *conflict,
            SharpSvn::Implementation::AprPool^ pool,
            String^ fullPath,
            bool hasTreeConflict,
            bool hasTextConflict,
            int propConflictCount,
            String^ incomingChangeSummary,
            String^ localChangeSummary);

        property SvnClient^ Owner
        {
            SvnClient^ get()
            {
                return _owner;
            }
        }

        property svn_client_conflict_t *Handle
        {
            svn_client_conflict_t *get()
            {
                Ensure();
                return _conflict;
            }
        }

        property SharpSvn::Implementation::AprPool^ Pool
        {
            SharpSvn::Implementation::AprPool^ get()
            {
                Ensure();
                return _pool;
            }
        }

        void Ensure();
        void SetTreeDescriptions(String^ incomingChangeSummary, String^ localChangeSummary);
        void SetRepositoryMetadata(Uri^ repositoryRoot, String^ repositoryUuid);
        void SetIncomingOldLocation(String^ repositoryPath, __int64 revision, SvnNodeKind nodeKind);
        void SetIncomingNewLocation(String^ repositoryPath, __int64 revision, SvnNodeKind nodeKind);

    public:
        ~SvnConflictInfo();
        !SvnConflictInfo();

        property String^ FullPath
        {
            String^ get()
            {
                return _fullPath;
            }
        }

        property String^ Path
        {
            String^ get()
            {
                return FullPath;
            }
        }

        property bool HasTreeConflict
        {
            bool get()
            {
                return _hasTreeConflict;
            }
        }

        property bool HasTextConflict
        {
            bool get()
            {
                return _hasTextConflict;
            }
        }

        property bool HasPropConflict
        {
            bool get()
            {
                return PropConflictCount > 0;
            }
        }

        property int PropConflictCount
        {
            int get()
            {
                return _propConflictCount;
            }
        }

        property SvnOperation Operation
        {
            SvnOperation get()
            {
                return _operation;
            }
        }

        property SvnConflictAction IncomingChange
        {
            SvnConflictAction get()
            {
                return _incomingChange;
            }
        }

        property SvnConflictReason LocalChange
        {
            SvnConflictReason get()
            {
                return _localChange;
            }
        }

        property SvnNodeKind VictimNodeKind
        {
            SvnNodeKind get()
            {
                return _victimNodeKind;
            }
        }

        property SvnConflictOptionId RecommendedOptionId
        {
            SvnConflictOptionId get()
            {
                return _recommendedOptionId;
            }
        }

        property String^ IncomingChangeSummary
        {
            String^ get()
            {
                return _incomingChangeSummary;
            }
        }

        property String^ LocalChangeSummary
        {
            String^ get()
            {
                return _localChangeSummary;
            }
        }

        property Uri^ RepositoryRoot
        {
            Uri^ get()
            {
                return _repositoryRoot;
            }
        }

        property String^ RepositoryUuid
        {
            String^ get()
            {
                return _repositoryUuid;
            }
        }

        property String^ IncomingOldRepositoryPath
        {
            String^ get()
            {
                return _incomingOldRepositoryPath;
            }
        }

        property __int64 IncomingOldRevision
        {
            __int64 get()
            {
                return _incomingOldRevision;
            }
        }

        property SvnNodeKind IncomingOldNodeKind
        {
            SvnNodeKind get()
            {
                return _incomingOldNodeKind;
            }
        }

        property String^ IncomingNewRepositoryPath
        {
            String^ get()
            {
                return _incomingNewRepositoryPath;
            }
        }

        property __int64 IncomingNewRevision
        {
            __int64 get()
            {
                return _incomingNewRevision;
            }
        }

        property SvnNodeKind IncomingNewNodeKind
        {
            SvnNodeKind get()
            {
                return _incomingNewNodeKind;
            }
        }
    };
}
