#include "header.h"
#include "iotools.h"

#include <opencv2/opencv.hpp>
#include <fstream>

using namespace std;
using namespace cv;

int main(int argc, char** argv){
    double t[2]; // timing
    t[0] = get_wall_time(); // Start Time
    if (argc < 3){
        cout << "Usage: get_precomp_feats feature_ids_file_name feature_file_name [base_updatepath num_bits normalize_features]" << std::endl;
        return -1;
    }
    
    PathManager pm;
    int feature_dim = 4096;
    int norm = true;
    // we just use hashing files to know number of features per update here.
    // so if multiple hashing are computed, use the smallest number of bits available.
    int bit_num = 256;
    string ids_file(argv[1]);
    string out_file(argv[2]);
    if (argc>3)
        pm.base_updatepath = argv[3];
    if (argc>4)
        bit_num = atoi(argv[4]);
    if (argc>5)
        norm = atoi(argv[5]);
    pm.set_paths(norm, bit_num);
    
    // debug
    cout << "base_updatepath in get_precomp_feats is: " << pm.base_updatepath << endl;

    //read in query
    int query_num = (int)filesize(argv[1])/sizeof(int);
    ifstream read_in(argv[1],ios::in|ios::binary);
    if (!read_in.is_open())
    {
        std::cout << "Cannot load the query feature ids file!" << std::endl;
        return -1;
    }
    // read query ids
    int* query_ids = new int[query_num];
    size_t read_size = sizeof(int)*query_num;
    read_in.read((char*)query_ids, read_size);
    read_in.close();

    float* feature = new float[query_num*feature_dim*sizeof(float)];
    read_size = sizeof(float)*feature_dim;
    char* feature_cp = (char*)feature;

    int status = get_n_features(query_ids, query_num, norm, bit_num, read_size, feature_cp, pm);
    if (status==-1) {
        std::cout << "Could not get features. Exiting." << std::endl;
        // TODO: We should clean here
        return -1;
    }
    // write out features to out_file
    //cout << "Will write feature to " << out_file << endl;
    ofstream output(out_file,ofstream::binary);
    feature_cp = (char*)feature;
    for (int i = 0; i<query_num; i++) {
        output.write(feature_cp,read_size);
        feature_cp +=  read_size;
    }
    output.close();
    cout << "Feature saved to " << out_file << endl;

    // Cleaning
    delete[] query_ids;
    delete[] feature;

    cout << "[get_precomp_feats] Total time (seconds): " << (float)(get_wall_time() - t[0]) << std::endl;
    return 0;
}

