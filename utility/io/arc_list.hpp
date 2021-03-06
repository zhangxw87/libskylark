#ifndef SKYLARK_ARC_LIST_HPP_
#define SKYLARK_ARC_LIST_HPP_

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <climits>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

// XXX: add a Boost serializer for our edge tuples: (index, index, value)
namespace boost { namespace serialization {

    template<class Archive, typename index_t, typename value_t>
    void serialize(Archive & ar, std::tuple<index_t, index_t, value_t> &t,
                   const unsigned int version) {
        ar & std::get<0>(t);
        ar & std::get<1>(t);
        ar & std::get<2>(t);
    }

}  // namespace serialization
}  // namespace boost


namespace skylark { namespace utility { namespace io {

// FIXME: move to io util header
namespace detail {

template <typename value_t>
void local_insert(
    base::sparse_vc_star_matrix_t<value_t>& X,
    const std::vector<std::tuple<El::Int, El::Int, value_t> >& edge_list) {

    assert(X.is_finalized() == false);

    typedef std::tuple<El::Int, El::Int, El::Int> tuple_type;
    std::vector<tuple_type>::const_iterator itr;

    for (size_t i = 0; i < edge_list.size(); i++)
        X.queue_update(
            get<0>(edge_list[i]), get<1>(edge_list[i]), get<2>(edge_list[i]));

    X.finalize();
}

/**
 * Parse local buffer and insert edges into temporary lists for target
 * processors.
 *
 * FIXME: target_rank computation only works for VC/STAR distribution (current
 * implementation). Better to use the Owner method to compute the target rank.
*/
template <typename edge_list_t, typename value_t>
void parse(std::stringstream& data, std::vector<edge_list_t>& proc_edge_list,
        size_t& max_row_idx, size_t& max_col_idx,
        boost::mpi::communicator &comm, bool symmetrize) {
    std::string line;
    while (std::getline(data, line)) {
        if (!data.good() && !data.eof())
            SKYLARK_THROW_EXCEPTION(
                base::io_exception() << base::error_msg("Stream went bad!"));

        if (line[0] == '#')
            continue;

        std::vector<std::string> values;
        boost::split(values, line, boost::is_any_of("\t "));

        size_t from = 0, to = 0;

        if (values.size() < 2) {
            std::stringstream err;
            err << "Invalid line \"" << line << "\""
                << std::endl;
            SKYLARK_THROW_EXCEPTION(
                base::io_exception() << base::error_msg(err.str()));
        }

        try {
            from = boost::lexical_cast<size_t>(values[0]);
        } catch(boost::bad_lexical_cast &) {
            std::stringstream err;
            err << "Cannot convert: \"" << values[0]
               << "\" of line \""
               << line << "\"" << std::endl;
            SKYLARK_THROW_EXCEPTION(
                base::io_exception() << base::error_msg(err.str()));
        }

        try {
            to = boost::lexical_cast<size_t>(values[1]);
        } catch(boost::bad_lexical_cast &) {
            std::stringstream err;
            err << "Cannot convert: \"" << values[1]
                << "\" of line \""
                << line << "\"" << std::endl;
            SKYLARK_THROW_EXCEPTION(
                base::io_exception() << base::error_msg(err.str()));
        }

        value_t value = 1.0;
        if (values.size() > 2) {
            try {
                value = boost::lexical_cast<value_t>(values[2]);
            } catch(boost::bad_lexical_cast &) {
                std::stringstream err;
                err << "Cannot convert: \"" << values[2]
                    << "\" of line \""
                    << line << "\"" << std::endl;
                SKYLARK_THROW_EXCEPTION(
                    base::io_exception() << base::error_msg(err.str()));
            }
        }

        max_col_idx = std::max(to, max_col_idx);
        max_row_idx = std::max(from, max_row_idx);

        size_t target_rank = from % comm.size();

        if (symmetrize) {
            proc_edge_list[target_rank].push_back(std::make_tuple(from, to, value / 2));
            target_rank = to % comm.size();
            proc_edge_list[target_rank].push_back(std::make_tuple(to, from, value / 2));
        } else {
            proc_edge_list[target_rank].push_back(std::make_tuple(from, to, value));
        }
    }

    if (symmetrize) {
        size_t dim = std::max(max_col_idx, max_row_idx);
        max_col_idx = dim;
        max_row_idx = dim;
    }
}

/**
 * Reads a text file in chunks and ensures that each partition/rank gets
 * full lines.
 *
 * \param fname name of the file
 * \param comm (sub-)communicator to read the and distribute the file
 * \param num_partitions number of partitions to split the file
 * \param data stream of read data
*/
void parallelChunkedRead(
        const std::string& fname, boost::mpi::communicator &comm,
        int num_partitions, std::stringstream& data) {

    int rank = comm.rank();

    MPI_File file;
    int rc = MPI_File_open(comm, const_cast<char *>(fname.c_str()),
                  MPI_MODE_RDONLY, MPI_INFO_NULL, &file);
     if (rc)
        SKYLARK_THROW_EXCEPTION(
            base::io_exception() << base::error_msg("Unable to open file"));

    // First we just divide the file across all the available processors
    // in the communicator.
    MPI_Offset size;
    MPI_File_get_size(file, &size);

    size_t myStart = static_cast<size_t>(static_cast<double>(rank) *
                     (static_cast<double>(size) / num_partitions));

    size_t myEnd = static_cast<size_t>(static_cast<double>(rank + 1) *
                   (static_cast<double>(size) / num_partitions));

    if (rank == num_partitions)
        myEnd = size;

    size_t mySize  = myEnd - myStart;

    // in case we rely on MPI I/O we need to make sure the max buffer size
    // is respected
    if (comm.size() > 1 && mySize > static_cast<size_t>(INT_MAX)) {
        std::ostringstream os;
        os << "ERROR: file " << fname << " cannot be opened."
           << std::endl
           << "Current implementation does not support one process "
           << "to read more than " << INT_MAX << " bytes (limited "
           << "by INT_MAX)."
           << std::endl;
        os << "To avoid this problem, use more processes to read the data."
           << std::endl;
        SKYLARK_THROW_EXCEPTION(
            base::io_exception() << base::error_msg(os.str()));
    }

    int err = 0;
    if (num_partitions == 1) {
        std::ifstream infile(fname);
        if (infile) {
            data << infile.rdbuf();
            infile.close();
        }
    } else {
        // Reading a portion of the file that is an initial guess of what
        // should belong to this process. Might not consist of entire lines,
        // balance later.
        std::vector<char> buff(mySize);
        MPI_Status readStatus;
        MPI_Offset offset = myStart;

        err =
            MPI_File_read_at(file, offset, &buff[0], mySize,
                MPI_BYTE, &readStatus);
        if (err != MPI_SUCCESS)
            SKYLARK_THROW_EXCEPTION(
                base::io_exception()
                    << base::error_msg("Error while MPI_File_read_ordered!"));

        data << std::string(buff.begin(), buff.end());
    }

    if (num_partitions > 1) {
        // now we go about "redistributing" (reading the correct offsets)
        // corresponding to the underlaying distribution.
        // 1) we need to figure out where our line ends and read the rest of the
        //    line.
        // 2) we need to find out what rows/cols we own and comm (?)
        //    the appropriate values

        MPI_Request recReq, sendReq;
        MPI_Status  recStatus, sendStatus;

        int myNumExtraBytes   = 0;
        int prevNumExtraBytes = 0;

        // pre-post receives
        if (rank != num_partitions - 1) {
            // expecting to receive the number of bytes until the first
            // endline character of the next portion
            MPI_Irecv(&myNumExtraBytes, 1, MPI_INT, rank + 1,
                      0, comm, &recReq);
        }

        // make sure that we have "full" lines on each processor
        if (rank == 0) {
            // and not sending anything just waiting for
            // the number of extra bytes to arrive
            MPI_Wait(&recReq, &recStatus);
        } else {
            std::string firstLine;
            // find the position of the first endline
            std::getline(data, firstLine);
            prevNumExtraBytes = data.tellg();
            if (data.eof() && rank != num_partitions - 1) {
                // this ranks data does not contain any line ending, so send
                // all my data preceding rank
                prevNumExtraBytes = mySize;

                MPI_Wait(&recReq, &recStatus);
                prevNumExtraBytes += myNumExtraBytes;

                myNumExtraBytes = 0;
                MPI_Isend(&prevNumExtraBytes, 1, MPI_INT, rank - 1,
                          0, comm, &sendReq);
            } else {
                // did find a line ending, send the number of bytes to
                // preceding rank
                MPI_Isend(&prevNumExtraBytes, 1, MPI_INT, rank - 1,
                          0, comm, &sendReq);
                if (rank == num_partitions - 1)
                    myNumExtraBytes = 0;
                else
                    MPI_Wait(&recReq, &recStatus);

                // strip the first prevNumExtraBytes from the stream
                //data.rdbuf()->pubsetbuf(&buff[prevNumExtraBytes],
                                        //mySize - prevNumExtraBytes);
            }
        }

        // Reading the extra bytes at the end.
        MPI_Status extraReadStatus;
        if (myNumExtraBytes > 0) {
            std::vector<char> extbuff(myNumExtraBytes);
            err = MPI_File_read_at(file, myEnd, &extbuff[0], myNumExtraBytes,
                                   MPI_BYTE, &extraReadStatus);
            if (err != MPI_SUCCESS)
                SKYLARK_THROW_EXCEPTION(
                    base::io_exception()
                        << base::error_msg("Error while MPI_file_read_at!"));

            // append the extra data
            data.seekg(0, data.end);
            data << std::string(extbuff.begin(), extbuff.end());
        }

        // waiting for the completion of all send requests
        if (rank != 0) MPI_Wait(&sendReq, &sendStatus);

        data.seekp(prevNumExtraBytes);
        data.seekg(prevNumExtraBytes);
    }

    MPI_File_close(&file);
}

}  // namespace detail


/**
 *  Read arc list, assuming the file contains triplets (from, to, weigh)
 *  separated by spaces or tabs.
 *
 *  Current implementation reads a "fair" part of the file and determines from
 *  the read indices which portions it should get. This corresponds to
 *  a block read, computing a distributed index map and then do a final
 *  communication step to get the local values.
 *
 *  Determines an initial guess of where the current process should
 *  start reading the file. Can also be used to find where to stop
 *  using the rank following the rank of this process.
 *  Current implementation aims to divide the file into equal portions
 *  in bytes.
 *
 *  @param fname input file name
 *  @param X output distributed sparse matrix
 *  @param comm MPI communicator reading and distributing the file
 *  @param symmetrize make the matrix symmetric by returning (A + A')/2
 */
template <typename value_t>
void ReadArcList(const std::string& fname,
    base::sparse_vc_star_matrix_t<value_t>& X,
    boost::mpi::communicator &comm, bool symmetrize = false) {

    assert(X.is_finalized() == false);

    typedef std::tuple<El::Int, El::Int, value_t> tuple_type;
    typedef std::vector<tuple_type> edge_list_t;

    int rank = comm.rank();
    int num_partitions = comm.size();

    std::stringstream data;
    detail::parallelChunkedRead(fname, comm, num_partitions, data);

    std::vector<edge_list_t> proc_set(comm.size());
    size_t max_row_idx = 0, max_col_idx = 0;
    detail::parse<edge_list_t, value_t>(data, proc_set,
        max_row_idx, max_col_idx, comm, symmetrize);

    boost::mpi::all_reduce(comm, boost::mpi::inplace_t<size_t>(max_row_idx),
        boost::mpi::maximum<size_t>());
    boost::mpi::all_reduce(comm, boost::mpi::inplace_t<size_t>(max_col_idx),
        boost::mpi::maximum<size_t>());

    X.resize(max_row_idx + 1, max_col_idx + 1);

    if (comm.size() == 1)
        return detail::local_insert(X, proc_set[0]);

    // finally we can redistribute the data, create a plan
    std::vector<size_t> proc_count(comm.size(), 0);
    for (size_t i = 0; i < comm.size(); i++)
        proc_count[i] = proc_set[i].size();

    // XXX: what comm strategy: p2p, collective, one-sided?
    // first communicate sizes that we will receive from other procs
    std::vector< std::vector<size_t> > vector_proc_counts;
    boost::mpi::all_gather(comm, proc_count, vector_proc_counts);

    size_t total_count = 0;
    for (size_t i = 0; i < vector_proc_counts.size(); ++i)
        total_count += vector_proc_counts[i][rank];

    // creating a local structure to hold sparse data triplets
    std::vector<tuple_type> matrix_data;
    try {
        matrix_data.resize(total_count);
    } catch (std::bad_alloc &e) {
        SKYLARK_THROW_EXCEPTION(
            base::allocation_exception() << base::error_msg("Out of memory."));
    }

    // FIXME: for now use synchronous comm, because Boost has an issue with
    //        comms:
    //  https://www.mail-archive.com/boost-mpi@lists.boost.org/msg00080.html
    //
    // pre-post receives
    // size_t idx = 0;
    std::vector<boost::mpi::request> requests(vector_proc_counts.size());
#if 0
    for (size_t i = 0; i < vector_proc_counts.size(); ++i) {
        // actual number of tuples we will receive from rank i
        size_t size = vector_proc_counts[i][rank];

        // pre-post receive for message of size of rank i
        requests[i] = comm.irecv(i, i, &(matrix_data[idx]), size);

        idx += size;
    }
#endif

    // send data
    for (size_t i = 0; i < proc_set.size(); ++i) {
        const edge_list_t &edges = proc_set[i];
        if (edges.size() == 0) continue;
        requests[i] = comm.isend(
                i, (i * comm.size()) + rank, &(edges[0]), edges.size());
    }

    size_t idx = 0;
    for (size_t i = 0; i < vector_proc_counts.size(); ++i) {
        // actual number of tuples we will receive from rank i
        size_t size = vector_proc_counts[i][rank];
        if (size == 0) continue;
        comm.recv(i, (rank * comm.size()) + i, &matrix_data[idx], size);
        idx += size;
    }

    // and wait for all requests to finish
    boost::mpi::wait_all(&requests[0],
        &requests[vector_proc_counts.size() - 1]);

    // insert all values the processor owns
    typename std::vector<tuple_type>::iterator matrix_itr;
    for (matrix_itr = matrix_data.begin(); matrix_itr != matrix_data.end();
        matrix_itr++) {
        // converts global to local
        X.queue_update(
            get<0>(*matrix_itr), get<1>(*matrix_itr), get<2>(*matrix_itr));
    }

    X.finalize();
}


/**
 *  Read arc list, assuming the file contains triplets (from, to, weigh)
 *  separated by spaces or tabs on every processor into a local sparse matrix.
 *
 *  @param fname input file name
 *  @param X output local sparse matrix
 *  @param comm MPI communicator reading and distributing the file
 *  @param symmetrize make the matrix symmetric by returning (A + A')/2
 */
template <typename value_t>
void ReadArcList(const std::string& fname,
    base::sparse_matrix_t<value_t>& X,
    boost::mpi::communicator &comm,
    bool symmetrize = false) {

    boost::mpi::communicator self(MPI_COMM_SELF, boost::mpi::comm_attach);

    std::stringstream data;
    std::ifstream file(fname);
    if (file) {
        data << file.rdbuf();
        file.close();
    } else {
        std::stringstream err;
        err << "Cannot open file \"" << fname << "\".";
        SKYLARK_THROW_EXCEPTION(
            base::io_exception() << base::error_msg(err.str()));
    }

    typedef typename base::sparse_matrix_t<value_t>::coord_tuple_t
        coord_tuple_t;
    typedef std::vector<coord_tuple_t> edge_list_t;
    std::vector<edge_list_t> edge_list(1);
    size_t max_row_idx = 0, max_col_idx = 0;
    detail::parse<edge_list_t, value_t>(data, edge_list,
        max_row_idx, max_col_idx, self, symmetrize);
    X.set(edge_list[0], max_row_idx + 1, max_col_idx + 1);
}


template <typename T>
void ReadArcList(const std::string& fname,
    El::Matrix<T>& X,
    boost::mpi::communicator &comm,
    bool symmetrize = false) {

    // TODO: should we add this?
    SKYLARK_THROW_EXCEPTION(
        base::unsupported_base_operation()
            << base::error_msg("ReadArcList El::Matrix not implemented."));
}


template <typename value_t, El::Distribution U, El::Distribution V>
void ReadArcList(const std::string& fname,
    El::DistMatrix<value_t, U, V>& X,
    boost::mpi::communicator &comm,
    bool symmetrize = false) {

    // TODO: should we add this?
    SKYLARK_THROW_EXCEPTION(
        base::unsupported_base_operation()
            << base::error_msg("ReadArcList El::DistMatrix not implemented."));
}


}  // namespace io
}  // namespace utility
}  // namespace skylark

#endif  // SKYLARK_ARC_LIST_HPP_
