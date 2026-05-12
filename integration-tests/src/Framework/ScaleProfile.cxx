// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/ScaleProfile.hxx"

namespace ese::tests
{

namespace
{

ScaleProfile _activeProfile = {};

}  //  namespace

const ScaleProfile& ActiveScaleProfile()
{
    return _activeProfile;
}

void SetActiveScaleProfile(ScaleProfileSize size)
{
    _activeProfile.Size = size;
}

int RowCount()
{
    switch (_activeProfile.Size)
    {
        case ScaleProfileSize::Small:
            return _activeProfile.SmallRowCount;
        case ScaleProfileSize::Medium:
            return _activeProfile.MediumRowCount;
        case ScaleProfileSize::Large:
            return _activeProfile.LargeRowCount;
    }
    return _activeProfile.SmallRowCount;
}

int TableCount()
{
    switch (_activeProfile.Size)
    {
        case ScaleProfileSize::Small:
            return _activeProfile.SmallTableCount;
        case ScaleProfileSize::Medium:
            return _activeProfile.MediumTableCount;
        case ScaleProfileSize::Large:
            return _activeProfile.LargeTableCount;
    }
    return _activeProfile.SmallTableCount;
}

int LongValueBytes()
{
    switch (_activeProfile.Size)
    {
        case ScaleProfileSize::Small:
            return _activeProfile.SmallLongValueBytes;
        case ScaleProfileSize::Medium:
            return _activeProfile.MediumLongValueBytes;
        case ScaleProfileSize::Large:
            return _activeProfile.LargeLongValueBytes;
    }
    return _activeProfile.SmallLongValueBytes;
}

}  //  namespace ese::tests
