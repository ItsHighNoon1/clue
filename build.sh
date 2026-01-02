echo "[Building server]"
cd server
bash build.sh
cd -
cd clients
for dir in */; do
    if [ -f $dir/build.sh ]; then
        echo "[Building client $dir]"
        cd $dir
        bash build.sh
        cd ..
    fi
done