//
// Created by ndp on 12/4/20.
//

#ifndef HYRAX_GIT_SUPERCHUNK_H
#define HYRAX_GIT_SUPERCHUNK_H


#include <vector>
#include <Chunk.h>
#include "DmrppArray.h"


namespace dmrpp {

    class SuperChunk {
    private:
        DmrppArray *d_parent;
        std::vector<const dmrpp::Chunk *> d_chunks;
        unsigned long long d_offset;
        unsigned long long d_size;

        bool is_contiguous(const dmrpp::Chunk &chunk);
        void map_chunks_to_buffer(unsigned char *r_buff);
        unsigned long long  read_contiguous(unsigned char *r_buff);

    public:
        explicit SuperChunk(DmrppArray *parent);
        ~SuperChunk() = default;;
        virtual bool add_chunk(const dmrpp::Chunk &chunk);

        virtual unsigned long long offset(){ return d_offset; };
        virtual unsigned long long size(){ return d_size; }

        virtual void read();
        virtual bool empty(){ return d_chunks.empty(); };

        std::string to_string(bool verbose);
    };

}// namespace dmrpp


#endif //HYRAX_GIT_SUPERCHUNK_H
