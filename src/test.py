def partition(base, s, e):
    e -= 1;
    pivot = s

    s +=1
    
    while(s < e):
        if(base[s] > base[e]):
            tmp = base[s]
            base[s] = base[e]
            base[e] = tmp

        if(base[s] > left_max):
            left_max = base[s];
            left_max_index = s;

        s += 1
        e -= 1


    if(e != left_max_index):
        if(base[e] < left_max):
            tmp = base[left_max_index]
            base[left_max_index] = base[e]
            base[e] = tmp

    return e;

def quick_sort(base, s, e):
    if(s < e and s < e-1):
        pivot = partition(base, s, e);
        quick_sort(base, s, pivot);
        quick_sort(base, pivot+1, e);

a = [3,2, 1, 0,5,3]
quick_sort(a, 0, len(a))
print(a)