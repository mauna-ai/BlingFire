/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 */

#ifndef _FA_TOKENSEGMENTATIONTOOLS_1BEST_BPE_T_H_
#define _FA_TOKENSEGMENTATIONTOOLS_1BEST_BPE_T_H_

#include "FAConfig.h"
#include "FARSDfaCA.h"
#include "FAMealyDfaCA.h"
#include "FAArrayCA.h"
#include "FAMultiMapCA.h"
#include "FADictConfKeeper.h"
#include "FALimits.h"
#include "FASecurity.h"
#include <cstdlib>
#include <vector>
#include <stdio.h>

///
/// Splits input sequence into segments using BPE algorithm.
///
/// Input:  sequence of characters
/// Output: array of tuples <TokenId, From, To>
///

template < class Ty >
class FATokenSegmentationTools_1best_bpe_t {

public:
    FATokenSegmentationTools_1best_bpe_t ();

public:
    /// initializes from the valid configuration object
    void SetConf (const FADictConfKeeper * pConf);

    /// writes an array of tuples <TokenId, From, To> into pOut
    /// returns the actual / needed size of the array to fit all the tuples or
    ///  -1 in case of an error
    const int Process (
            const Ty * pIn,
            const int InSize,
            __out_ecount(MaxOutSize) int * pOut,
            const int MaxOutSize,
            const int UnkId
        ) const;

private:
    // Mealy DFA keeping a map from a known segment to idx and
    // and MultiMap keeping a realtion between idx and <ID, Score> pair
    const FARSDfaCA * m_pDfa;
    const FAMealyDfaCA * m_pMealy;
    const FAArrayCA * m_pK2I;     // note this is an identify since we don't have duplicate ID's
    const FAMultiMapCA * m_pI2Info;

    // to keep track of arc data (note we use ID as a score for BPE since it follows strict ordering)
    struct _TArc {

        int _Start;    // the begging position of the segment
        int _End;      // the ending position of the segment
        int _Id;       // ID of a segment from the vocab

    public:
        _TArc ():
            _Start(0),
            _End(0),
            _Id(0)
        {}

        _TArc (int b, int e, int id):
            _Start(b),
            _End(e),
            _Id(id)
        {}

    };

};


template < class Ty >
FATokenSegmentationTools_1best_bpe_t < Ty >::
    FATokenSegmentationTools_1best_bpe_t () :
        m_pDfa (NULL),
        m_pMealy (NULL),
        m_pK2I (NULL),
        m_pI2Info (NULL)
{}


template < class Ty >
void FATokenSegmentationTools_1best_bpe_t < Ty >::
    SetConf (const FADictConfKeeper * pConf)
{
    LogAssert (pConf);
    LogAssert(FAFsmConst::TYPE_MEALY_DFA == pConf->GetFsmType());

    m_pDfa = pConf->GetRsDfa ();
    m_pMealy = pConf->GetMphMealy ();
    m_pK2I = pConf->GetK2I ();
    m_pI2Info = pConf->GetI2Info ();

    LogAssert(0 < m_pK2I->GetCount ());
}


template < class Ty >
const int FATokenSegmentationTools_1best_bpe_t < Ty >::
    Process (
        const Ty * pIn, 
        const int InSize, 
        __out_ecount(MaxOutSize) int * pOut,
        const int MaxOutSize,
        const int UnkId
    ) const
{
    DebugLogAssert (m_pDfa && m_pMealy && m_pK2I && m_pI2Info);

    if (0 >= InSize) {
        return 0;
    }

    LogAssert (pIn && InSize <= FALimits::MaxArrSize);

    // allocate storage for all segments found in the text
    std::vector <_TArc> arcs;
    arcs.reserve(InSize);

    // get the initial state
    const int InitialState = m_pDfa->GetInitial ();

    // populate the arcs
    for (int start = 0; start < InSize; ++start) {

        int State = InitialState;
        int SumOw = 0;
        int Ow = 0;
        bool TokenUnknown = true;

        // go as deep as we can from the start position
        for (int i = start; i < InSize; ++i) {

            const Ty Iw = pIn [i];
            State = m_pMealy->GetDestOw (State, Iw, &Ow);

            // see if the does not have a transition
            if (-1 == State) {
                break;
            }

            SumOw += Ow;
            DebugLogAssert (0 <= Ow);

            // see if the destination state is a final state
            if (m_pDfa->IsFinal (State)) {

                // look up the id of the segment, we ignore the score if it is there
                const int * pValues = NULL;
                const int Count = m_pI2Info->Get (SumOw, &pValues);
                LogAssert (1 <= Count && NULL != pValues);

                // get the ID
                const int id = pValues [0];

                // add the arc
                arcs.push_back(_TArc(start, i, id));
                TokenUnknown = false;
            }

        } // of for(int i = start; i < InSize; ++start) ...

        if (TokenUnknown) {
            // if we are here then nothing matched from the start

            // check if the prevous arc is also unknown
            const int ArcCount = arcs.size();
            if (0 < ArcCount && UnkId == arcs [ArcCount - 1]._Id) {
                // modify previous arc (make unknown segment longer)
                arcs [ArcCount - 1]._End = start;
            } else {
                // add a new unknown arc
                arcs.push_back(_TArc(start, start, UnkId));
            }
        }

    } // for(int start = 0; start < InSize; ++start) ...

    // sort the arcs
    _TArc * pArcs = arcs.data();
    const size_t ArcCount = arcs.size();

    std::qsort(pArcs, ArcCount, sizeof(_TArc), [](const void* a, const void* b) {
        const _TArc* pA = static_cast<const _TArc*>(a);
        const _TArc* pB = static_cast<const _TArc*>(b);
        // smaller ids first
        if (pA->_Id < pB->_Id) { 
            return -1; 
        } else if (pA->_Id == pB->_Id) {
            // if ids are the same left-most first
            if (pA->_Start < pB->_Start) {
                return -1;
            } else if (pA->_Start == pB->_Start) {
                return 0;
            } else {
                return 1;
            }
        }
        return 1;
    });

    // keep track of the from --> to, from --> id and intermediate positions
    std::vector <int> tos_ids (InSize * 3, 0);

    // all 0's
    int * pTos = tos_ids.data ();

    // all UnkId's
    int * pIds = pTos + InSize;
    for(size_t i = 0; i < InSize; ++i) {
        pIds [i] = UnkId;
    }

    // point to the third array of ints and cast it to the array of bytes
    // Note: all of the values are set to 0
    unsigned char * pIntermediate = (unsigned char *)(pTos + (InSize * 2));

    // go over all acrs in order
    for(size_t i = 0; i < ArcCount; ++i) {

        const _TArc * pA = pArcs + i;
        const int Start = pA->_Start;
        const int End = pA->_End;

        // see start/end are avaible for the merge
        if(0 == pIntermediate [Start] && 
           (End + 1 == InSize || 0 == pIntermediate [End + 1])) {

            pTos [Start] = End;
            pIds [Start] = pA->_Id;

            // more efficient variant of:
            // for (int j = Start + 1; j <= End; ++j) {
            //     pIntermediate [j] = 1;
            // }
            const int IntermediateCount = End - Start;
            if (0 < IntermediateCount) {
                memset (pIntermediate + Start + 1, 1, IntermediateCount);
            }
        }
    }

    // copy the results
    int ActualOutSize = 0;
    for (int start = 0; start < InSize; start++) {

        const int end = pTos [start];
        const int id = pIds [start];

        if (ActualOutSize + 3 <= MaxOutSize) {
            pOut [ActualOutSize] = id;
            pOut [ActualOutSize + 1] = start;
            pOut [ActualOutSize + 2] = end;
        }
        ActualOutSize += 3;

        start = end; // and +1 will be added by the for loop
    }

    return ActualOutSize;
}

#endif
