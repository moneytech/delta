cd spec || exit
rm -rf ../build
mkdir ../build
pandoc spec.tex -o ../build/spec.html -s --toc --include-before-body="../top-nav.html" --include-before-body="../toc.html"
cd ..

for file in book/*.md index.html; do
    basename=$(basename "$file")

    case $basename in
        README.md)
            continue
            ;;
        index.html)
            toc=""
            ;;
        *)
            toc=--include-before-body="toc.html"
            ;;
    esac

    basename="${basename%.*}"
    pandoc "$file" -o "build/$basename.html" -s --include-before-body="top-nav.html" $toc --metadata pagetitle="Delta"
done

cp -r *.css *.js lib build
