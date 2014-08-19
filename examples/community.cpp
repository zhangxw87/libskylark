#include <iostream>

#include <elemental.hpp>
#include <boost/mpi.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <skylark.hpp>

#include <H5Cpp.h>

namespace bmpi =  boost::mpi;
namespace bpo = boost::program_options;
namespace skybase = skylark::base;
namespace skysketch =  skylark::sketch;
namespace skynla = skylark::nla;
namespace skyalg = skylark::algorithms;
namespace skyml = skylark::ml;
namespace skyutil = skylark::utility;

int main(int argc, char** argv) {


    elem::Initialize(argc, argv);

    boost::mpi::timer timer;

    // Parse options
    double gamma, alpha, epsilon;
    std::string graphfile;
    int seed;
    bpo::options_description
        desc("Options:");
    desc.add_options()
        ("help,h", "produce a help message")
        ("graphfile,g",
            bpo::value<std::string>(&graphfile),
            "File holding the graph. REQUIRED.")
        ("seed,s",
            bpo::value<int>(&seed),
            "Seed node (node numbers begin at 1). REQUIRED.")
        ("gamma",
            bpo::value<double>(&gamma)->default_value(5.0),
            "Time to derive the diffusion. As gamma->inf we get closer to ppr.")
        ("alpha",
            bpo::value<double>(&alpha)->default_value(0.8),
            "PPR component parameter. alpha=1 will result in pure heat-kernel.")
        ("epsilon",
            bpo::value<double>(&epsilon)->default_value(0.001),
            "Accuracy parameter for convergence.");

    bpo::variables_map vm;
    try {
        bpo::store(bpo::command_line_parser(argc, argv)
            .options(desc).run(), vm);

        if (vm.count("help")) {
            std::cout << desc;
            return 0;
        }

        if (!vm.count("graphfile")) {
            std::cout << "Input graph-file is required." << std::endl;
            return -1;
        }

        if (!vm.count("seed")) {
            std::cout << "Seed node is required." << std::endl;
            return -1;
        }

        bpo::notify(vm);
    } catch(bpo::error& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << desc << std::endl;
        return -1;
    }


    // Load A from HDF5 file
    skybase::sparse_matrix_t<double> A;
    std::cout << "Reading the adjacency matrix... ";
    std::cout.flush();
    timer.restart();
    H5::H5File in(graphfile, H5F_ACC_RDONLY);
    skyutil::io::ReadHDF5(in, "A", A);
    in.close();
    std::cout <<"took " << boost::format("%.2e") % timer.elapsed() << " sec\n";

    // TODO get list as parameter
    std::vector<int> seeds = {seed - 1};

    timer.restart();
    std::vector<int> cluster;
    double cond = skyml::FindLocalCluster(A, seeds, cluster, alpha, gamma, epsilon);
    std::cout <<"Analysis complete! Took "
              << boost::format("%.2e") % timer.elapsed() << " sec\n";
    std::cout << "Cluster found (node numbers begin at 1):" << std::endl;
    for (auto it = cluster.begin(); it != cluster.end(); it++)
        std::cout << *it + 1 << " ";
    std::cout << std::endl;
    std::cout << "Conductivity = " << cond << std::endl;

    return 0;
}
