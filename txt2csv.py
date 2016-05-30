import csv
wf=open('c:/docxml/out.csv', 'w', newline='')
wr=csv.writer(wf, delimiter=':', quotechar=' ')
with open('c:/docxml/res.csv') as csvfile:
    spamreader = csvfile.readlines()
    for row in spamreader:
        print(row)
        wr.writerow([row.split(':')[1],row.split(':')[2]])
wf.close()