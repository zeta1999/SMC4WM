#include "interface.hpp"
int main(int argc, char *argv[])
{
    interface in;

    char prop_file[] = "../testcase/prop";
    char model_file[] = "../testcase/cra_cag_praise_2019-01-18_01-13-25.dat";
    char folder[] = "../trace/test";
    string interfile = "../testcase/intervention";
    int result;
    if (argc == 7)
    {
        try
        {
           result = in.checkmodel(argv[1], argv[2], argv[3], atoi(argv[6]),argv[4], argv[5]);
        } 
        catch(...)
        {
            return -1;
        }
        
    }
    else if (argc == 6)
    {

        try
        {
            result = in.checkmodel(argv[1], argv[2], argv[3], atoi(argv[5]), "", argv[4]);
        } 
        catch(...)
        {
            return -1;
        }
    }
    else
    {

        try
        {
            result = in.checkmodel(model_file, prop_file, folder, 0, interfile, "../testcase/initConfig.txt");
        } 
        catch(...)
        {
            return -1;
        }
    }
    cout<<result<<endl;
    return result;
}
