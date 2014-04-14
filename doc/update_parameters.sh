cd ..
./aspect doc/manual/empty.prm >/dev/null 2>/dev/null
cp output/parameters.tex doc/manual/
cd doc/manual
echo patching parameters.tex
sed -i 's/LD_LIBRARY_PATH/LD\\_LIBRARY\\_PATH/g' parameters.tex
sed -i 's/tecplot_binary/tecplot\\_binary/g' parameters.tex
sed -i 's/\$ASPECT_SOURCE_DIR/\\\$ASPECT\\_SOURCE\\_DIR/g' parameters.tex
sed -i 's/<depth_average.ext>/<depth\\_average.ext>/g' parameters.tex
sed -i 's/dynamic_topography.NNNNN/dynamic\\_topography.NNNNN/g' parameters.tex

cd ..
echo done