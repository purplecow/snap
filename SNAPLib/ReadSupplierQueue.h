/*++

Module Name:

    ReadSupplierQueue.h

Abstract:

    Headers for parallel queue of reads

Authors:

    Bill Bolosky, November, 2012

Environment:

    User mode service.

Revision History:


--*/

#pragma once
#include "Read.h"
#include "Compat.h"

class ReadSupplierFromQueue;
class PairedReadSupplierFromQueue;

struct ReadQueueElement {
    ReadQueueElement() : next(NULL), prev(NULL) {}

    static const int nReads = 10000;
    ReadQueueElement    *next;
    ReadQueueElement    *prev;
    int                 totalReads;
    Read                reads[nReads];

    void addToTail(ReadQueueElement *queueHead) {
        prev = queueHead;
        next = queueHead->prev;
        prev->next = this;
        next->prev = this;
    }

    void removeFromQueue() {
        prev->next = next;
        next->prev = prev;
        prev = next = NULL;
    }
};
    
class ReadSupplierQueue {
public:
    //
    // This queue can handle several different kinds of inputs and outputs.  It will do either single
    // ended or paired reads.  In both cases, it can accept multiple independent readers (typically
    // one per (pair of) input file(s).  For paired reads that come from pairs of input files (think
    // FASTQ) it will run them independently and then combine the results as they're extracted.  For
    // paired reads that come from single files (SAM/BAM/CRAM, etc.) it still uses two queues internally,
    // but they're both written by a single PairedReadReader.
    //

    //
    // The version for single ended reads.  This is useful for formats that can't be divided by the
    // RangeSplitter, like BAM (though that's theoretically possible, so maybe..)  It takes a set 
    // of readers (presumably for different files), each of which runs independently and in parallel.
    // 
    ReadSupplierQueue(int i_nReaders, ReadReader **readers);

    //
    // The version for paired reads for which each end comes from a different Reader (and presumably
    // file, think FASTQ).  This is mostly useful for cases where the RangeSplitter can't handle
    // the files, probably because they FASTQ files with unequal length reads).
    //
    ReadSupplierQueue(int i_nReaders, ReadReader **firstHalfReaders, ReadReader **secondHalfReaders);

    //
    // The version for paired reads that come from a single file but for which RangeSplitter won't
    // work (BAM or CRAM or maybe SRA).
    //
    ReadSupplierQueue(int i_nReaders, PairedReadReader **pairedReaders);
    ~ReadSupplierQueue();

    bool startReaders();
    void waitUntilFinished();
    ReadSupplierFromQueue *createSupplier();
    PairedReadSupplierFromQueue *createPairedSupplier();

    ReadQueueElement *getElement();     // Called from the supplier threads
    bool getElements(ReadQueueElement **element1, ReadQueueElement **element2);   // Called from supplier threads
    void doneWithElement(ReadQueueElement *element);
    void supplierFinished();


private:

    void commonInit(int i_nReaders);

    //
    // A reader group is responsible for generating single or paired end reads
    // from one or two files.  It has its own queue(s).  The ReaderGroups
    // themselves may be on the queue of reader groups that have reads
    // available.
    //
    struct ReaderGroup {
        ReaderGroup() : next(NULL), prev(NULL), pairedReader(NULL) {
            singleReader[0] = singleReader[1] = NULL;
            readyQueue->next = readyQueue->prev = readyQueue;
            readyQueue[1].next = readyQueue[1].prev = &readyQueue[1];
        }

        ReaderGroup         *next;
        ReaderGroup         *prev;

        ReadReader          *singleReader[2];   // Only [0] is filled in for single ended reads
        PairedReadReader    *pairedReader;      // This is filled in iff there are no single readers

        ReadQueueElement     readyQueue[2];     // Queue [1] is used only when there are two single end readers

        void addToQueue(ReaderGroup *queue) {
            next = queue;
            prev = queue->prev;
            next->prev = this;
            prev->next = this;
        }

        void removeFromQueue() {
            next->prev = prev;
            prev->next = next;
            next = prev = NULL;
        }
    };

    ReaderGroup *readerGroups;
    ReaderGroup readerGroupsWithReadyReads[1];

    int nReaders;
    int nReadersRunning;
    int nSuppliersRunning;
    bool allReadsQueued;

    //
    // Empty buffers waiting for the readers.
    //
    ReadQueueElement emptyQueue[1];
  
    //
    // Just one lock for all of the shared objects (the queues and Waiter objects, and counts of
    // readers and suppliers running, as well as allReadsQueued).
    //
    ExclusiveLock lock;
    SingleWaiterObject readsReady;
    SingleWaiterObject emptyBuffersAvailable;

    SingleWaiterObject allReadsConsumed;

    struct ReaderThreadParams {
        ReadSupplierQueue       *queue;
        ReaderGroup             *group;
        bool                     isSecondReader;
    };

    static void ReaderThreadMain(void *);
    void ReaderThread(ReaderThreadParams *params);
};

//
// A read supplier that takes its data from a ReadSupplierQueue.
//
class ReadSupplierFromQueue: public ReadSupplier {
public:
    ReadSupplierFromQueue(ReadSupplierQueue *i_queue);
    ~ReadSupplierFromQueue() {}

    Read *getNextRead();

private:
    bool                done;
    ReadSupplierQueue   *queue;
    bool                outOfReads;
    ReadQueueElement    *currentElement;
    int                 nextReadIndex;          
};

class PairedReadSupplierFromQueue: public PairedReadSupplier {
public:
    PairedReadSupplierFromQueue(ReadSupplierQueue *i_queue, bool i_twoFiles);
    ~PairedReadSupplierFromQueue();

    bool getNextReadPair(Read **read0, Read **read1);

private:
    ReadSupplierQueue   *queue;
    bool                done;
    bool                twoFiles;
    ReadQueueElement    *currentElement;
    ReadQueueElement    *currentSecondElement;
    int                 nextReadIndex;          
};