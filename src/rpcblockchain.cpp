// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"
#include "init.h"
#include "txdb.h"
#include "kernel.h"
#include "checkpoints.h"
#include "protocol.h"
#include <errno.h>

using namespace json_spirit;
using namespace std;

// see protocol.h:28
#define MESSAGE_START_SIZE 4

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json_spirit::Object& entry);
extern enum Checkpoints::CPMode CheckpointsMode;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetPoWMHashPS()
{
    if (pindexBest->nHeight >= LAST_POW_BLOCK)
        return 0;

    int nPoWInterval = 72;
    int64_t nTargetSpacingWorkMin = 30, nTargetSpacingWork = 30;

    CBlockIndex* pindex = pindexGenesisBlock;
    CBlockIndex* pindexPrevWork = pindexGenesisBlock;

    while (pindex)
    {
        if (pindex->IsProofOfWork())
        {
            int64_t nActualSpacingWork = pindex->GetBlockTime() - pindexPrevWork->GetBlockTime();
            nTargetSpacingWork = ((nPoWInterval - 1) * nTargetSpacingWork + nActualSpacingWork + nActualSpacingWork) / (nPoWInterval + 1);
            nTargetSpacingWork = max(nTargetSpacingWork, nTargetSpacingWorkMin);
            pindexPrevWork = pindex;
        }

        pindex = pindex->pnext;
    }

    return GetDifficulty() * 4294.967296 / nTargetSpacingWork;
}

double GetPoSKernelPS()
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    CBlockIndex* pindex = pindexBest;;
    CBlockIndex* pindexPrevStake = NULL;

    while (pindex && nStakesHandled < nPoSInterval)
    {
        if (pindex->IsProofOfStake())
        {
            dStakeKernelsTriedAvg += GetDifficulty(pindex) * 4294967296.0;
            nStakesTime += pindexPrevStake ? (pindexPrevStake->nTime - pindex->nTime) : 0;
            pindexPrevStake = pindex;
            nStakesHandled++;
        }

        pindex = pindex->pprev;
    }

    return nStakesTime ? dStakeKernelsTriedAvg / nStakesTime : 0;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    CMerkleTx txGen(block.vtx[0]);
    txGen.SetMerkleBranch(&block);
    result.push_back(Pair("confirmations", (int)txGen.GetDepthInMainChain()));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (boost::int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (boost::uint64_t)block.nNonce));
    result.push_back(Pair("bits", HexBits(block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0')));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));

    result.push_back(Pair("flags", strprintf("%s%s", blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->IsProofOfStake()? blockindex->hashProofOfStake.GetHex() : blockindex->GetBlockHash().GetHex()));
    result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016"PRIx64, blockindex->nStakeModifier)));
    result.push_back(Pair("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum)));
    Array txinfo;
    BOOST_FOREACH (const CTransaction& tx, block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            Object entry;

            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            TxToJSON(tx, 0, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.push_back(Pair("tx", txinfo));

    if (block.IsProofOfStake())
        result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Value getbestblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    Object obj;
    obj.push_back(Pair("proof-of-work",        GetDifficulty()));
    obj.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    return obj;
}


Value settxfee(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < MIN_TX_FEE)
        throw runtime_error(
            "settxfee <amount>\n"
            "<amount> is a real and is rounded to the nearest 0.01");

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

Value getrawmempool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    Array a;
    BOOST_FOREACH(const uint256& hash, vtxid)
        a.push_back(hash.ToString());

    return a;
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock <hash> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-hash.");

    std::string strHash = params[0].get_str();
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

Value getblockbynumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

// ppcoin: get information of sync-checkpoint
Value getcheckpoint(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    Object result;
    CBlockIndex* pindexCheckpoint;

    result.push_back(Pair("synccheckpoint", Checkpoints::hashSyncCheckpoint.ToString().c_str()));
    pindexCheckpoint = mapBlockIndex[Checkpoints::hashSyncCheckpoint];
    result.push_back(Pair("height", pindexCheckpoint->nHeight));
    result.push_back(Pair("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str()));

    // Check that the block satisfies synchronized checkpoint
    if (CheckpointsMode == Checkpoints::STRICT)
        result.push_back(Pair("policy", "strict"));

    if (CheckpointsMode == Checkpoints::ADVISORY)
        result.push_back(Pair("policy", "advisory"));

    if (CheckpointsMode == Checkpoints::PERMISSIVE)
        result.push_back(Pair("policy", "permissive"));

    if (mapArgs.count("-checkpointkey"))
        result.push_back(Pair("checkpointmaster", true));

    return result;
}

Value rewindchain(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "rewindchain <number>\n"
            "Remove <number> blocks from the chain.");

    int nNumber = params[0].get_int();
    if (nNumber < 0 || nNumber > nBestHeight)
        throw runtime_error("Block number out of range.");

    Object result;
    int nRemoved = 0;

    {
    LOCK2(cs_main, pwalletMain->cs_wallet);


    uint32_t nFileRet = 0;

    uint8_t buffer[512];

    printf("rewindchain %d\n", nNumber);

    void* nFind;

    for (int i = 0; i < nNumber; ++i)
    {
        memset(buffer, 0, sizeof(buffer));

        FILE* fp = AppendBlockFile(nFileRet, "r+b");

        if (!fp)
        {
            printf("AppendBlockFile failed.\n");
            break;
        };

        errno = 0;
        if (fseek(fp, 0, SEEK_END) != 0)
        {
            printf("fseek failed: %s\n", strerror(errno));
            break;
        };

        long int fpos = ftell(fp);

        if (fpos == -1)
        {
            printf("ftell failed: %s\n", strerror(errno));
            break;
        };

        long int foundPos = -1;
        long int readSize = sizeof(buffer) / 2;
        while (fpos > 0)
        {
            if (fpos < (long int)sizeof(buffer) / 2)
                readSize = fpos;

            memcpy(buffer+readSize, buffer, readSize); // move last read data (incase token crosses a boundary)
            fpos -= readSize;

            if (fseek(fp, fpos, SEEK_SET) != 0)
            {
                printf("fseek failed: %s\n", strerror(errno));
                break;
            };

            errno = 0;
            if (fread(buffer, sizeof(uint8_t), readSize, fp) != (size_t)readSize)
            {
                if (errno != 0)
                    printf("fread failed: %s\n", strerror(errno));
                else
                    printf("End of file.\n");
                break;
            };

            uint32_t findPos = sizeof(buffer);
            while (findPos > MESSAGE_START_SIZE)
            {
                if ((nFind = erc::memrchr(buffer, pchMessageStart[0], findPos-MESSAGE_START_SIZE)))
                {
                    if (memcmp(nFind, pchMessageStart, MESSAGE_START_SIZE) == 0)
                    {
                        foundPos = ((uint8_t*)nFind - buffer) + MESSAGE_START_SIZE;
                        break;
                    } else
                    {
                        findPos = ((uint8_t*)nFind - buffer);
                        // -- step over matched char that wasn't pchMessageStart
                        if (findPos > 0) // prevent findPos < 0 (unsigned)
                            findPos--;
                    };
                } else
                {
                    break; // pchMessageStart[0] not found in buffer
                };
            };

            if (foundPos > -1)
                break;
        };

        printf("fpos %d, foundPos %d.\n", fpos, foundPos);

        if (foundPos < 0)
        {
            printf("block start not found.\n");
            fclose(fp);
            break;
        };

        CAutoFile blkdat(fp, SER_DISK, CLIENT_VERSION);

        if (fseek(blkdat, fpos+foundPos, SEEK_SET) != 0)
        {
            printf("fseek blkdat failed: %s\n", strerror(errno));
            break;
        };

        unsigned int nSize;
        blkdat >> nSize;
        printf("nSize %u .\n", nSize);

        if (nSize < 1 || nSize > MAX_BLOCK_SIZE)
        {
            printf("block size error %u\n", nSize);

        };

        CBlock block;
        blkdat >> block;
        uint256 hashblock = block.GetHash();
        printf("hashblock %s .\n", hashblock.ToString().c_str());

        std::map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashblock);
        if (mi != mapBlockIndex.end() && (*mi).second)
        {
            printf("block is in main chain.\n");

            if (!mi->second->pprev)
            {
                printf("! mi->second.pprev\n");
            } else
            {
                {
                    CBlock blockPrev; // strange way SetBestChain works, TODO: does it need the full block?
                    if (!blockPrev.ReadFromDisk(mi->second->pprev))
                    {
                        printf("blockPrev.ReadFromDisk failed %s.\n", mi->second->pprev->GetBlockHash().ToString().c_str());
                        break;
                    };

                    CTxDB txdb;
                    if (!blockPrev.SetBestChain(txdb, mi->second->pprev))
                    {
                        printf("SetBestChain failed.\n");
                    };
                }
                mi->second->pprev->pnext = NULL;
            };

            delete mi->second;
            mapBlockIndex.erase(mi);
        };

        std::map<uint256, CBlock*>::iterator miOph = mapOrphanBlocks.find(hashblock);
        if (miOph != mapOrphanBlocks.end())
        {
            printf("block is an orphan.\n");
            mapOrphanBlocks.erase(miOph);
        };

        CTxDB txdb;
        for (vector<CTransaction>::iterator it = block.vtx.begin(); it != block.vtx.end(); ++it)
        {
            printf("EraseTxIndex().\n");
            txdb.EraseTxIndex(*it);
        };

        printf("EraseBlockIndex().\n");
        txdb.EraseBlockIndex(hashblock);

        errno = 0;
        if (ftruncate(fileno(fp), fpos+foundPos-MESSAGE_START_SIZE) != 0)
        {
            printf("ftruncate failed: %s\n", strerror(errno));
        };

        printf("hashBestChain %s, nBestHeight %d\n", hashBestChain.ToString().c_str(), nBestHeight);

        //fclose(fp); // ~CAutoFile() will close the file
        nRemoved++;
    };
    }


    result.push_back(Pair("no. blocks removed", itostr(nRemoved)));

    result.push_back(Pair("hashBestChain", hashBestChain.ToString()));
    result.push_back(Pair("nBestHeight", itostr(nBestHeight)));   

    if (nRemoved == nNumber)
        result.push_back(Pair("result", "success"));
    else
        result.push_back(Pair("result", "failure"));

    // -- need restart, setStakeSeen etc
    if (nRemoved > 0)
        result.push_back(Pair("Further action", "Please restart Europecoin"));
    return result;
}
