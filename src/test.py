def partition(base, s, e):
    pivot = s
    e -= 1;
    s += 1

    while(s < e):
        while s <= e and base[s] <= base[pivot]:
            s += 1

        while s <= e and base[e] > base[pivot]:
            e -= 1

        if s < e:
            tmp = base[s]
            base[s] = base[e]
            base[e] = tmp

    if e > pivot and base[pivot] > base[e]:
        tmp = base[pivot]
        base[pivot] = base[e]
        base[e] = tmp

    print('y', base, e)
    return e;

def quick_sort(base, s, e):
    if(s < e and s < e-1):
        pivot = partition(base, s, e);
        quick_sort(base, s, pivot);
        quick_sort(base, pivot+1, e);

a = [1, 2, 3, 21, 32, 13, -1]

quick_sort(a, 0, len(a))
print(a)