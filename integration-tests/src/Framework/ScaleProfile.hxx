// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  ScaleProfile — selects a row-count / table-count / payload-size
//  bucket per scenario.  Scenarios read constants off the active profile
//  rather than hard-coding sizes, so the same code can run under fast
//  CI (Small), nightly (Medium), or long-running stress (Large).

#pragma once

namespace ese::tests
{

enum class ScaleProfileSize
{
    Small,
    Medium,
    Large,
};

struct ScaleProfile
{
    ScaleProfileSize Size                 = ScaleProfileSize::Small;
    int              SmallRowCount        = 100;
    int              MediumRowCount       = 100'000;
    int              LargeRowCount        = 10'000'000;
    int              SmallTableCount      = 4;
    int              MediumTableCount     = 32;
    int              LargeTableCount      = 1024;
    int              SmallLongValueBytes  = 4'096;
    int              MediumLongValueBytes = 1'048'576;
    int              LargeLongValueBytes  = 64 * 1'048'576;
};

const ScaleProfile& ActiveScaleProfile();
void SetActiveScaleProfile(ScaleProfileSize size);

//  Convenience accessors that pick the right bucket for the active size.
int RowCount();
int TableCount();
int LongValueBytes();

}  //  namespace ese::tests
